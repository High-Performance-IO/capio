#ifndef CAPIO_POSIX_HANDLERS_COPY_FILE_RANGE_HPP
#define CAPIO_POSIX_HANDLERS_COPY_FILE_RANGE_HPP

#if defined(SYS_copy_file_range)

#include <sys/mman.h>
#include <sys/stat.h>

#include "common/env.hpp"

#include "utils/common.hpp"
#include "utils/data.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"
#include "utils/types.hpp"

/**
 * Gather what a copy needs to know about @fd, and the position the copy starts at: the offset
 * passed by the caller, or else the descriptor offset.
 *
 * @param fd
 * @param offset the offset passed by the caller, or nullptr
 * @param tid
 * @return the end of the copy; is_open is false, and the other fields are not set, if @fd is not
 * an open descriptor
 */
inline CPCopyFileRangeEnd_t capio_copy_file_range_end(const int fd, loff_t *offset,
                                                      const long tid) {
    START_LOG(tid, "call(fd=%d, offset=0x%08x)", fd, offset);

    CPCopyFileRangeEnd_t end;
    end.fd       = fd;
    end.offset   = offset;
    end.is_capio = exists_capio_fd(fd);

    if (end.is_capio) {
        // the file size must count the data still in the write cache
        write_cache->flush();
        auto [file_size, is_dir] = fstat_request(fd, tid);
        end.is_open              = true;
        end.flags                = get_capio_fd_flags(fd);
        end.is_dir               = is_dir;
        end.is_regular           = !is_dir;
        end.size                 = file_size < 0 ? 0 : file_size;

        // the CAPIO caches hold offsets the server does not know yet: this is the offset the
        // application sees
        end.saved_fd_offset = get_capio_fd_offset(fd);
        end.position        = offset != nullptr ? *offset : end.saved_fd_offset;
        LOG("fd %d is a capio fd: flags=%d, is_dir=%s, size=%ld, position=%ld", fd, end.flags,
            end.is_dir ? "true" : "false", end.size, end.position);
        return end;
    }

    const long flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFL);
    struct stat statbuf{};
    if (flags < 0 || syscall_no_intercept(SYS_fstat, fd, &statbuf) < 0) {
        LOG("fd %d is not an open descriptor", fd);
        return end;
    }
    end.is_open    = true;
    end.flags      = static_cast<int>(flags);
    end.is_dir     = S_ISDIR(statbuf.st_mode);
    end.is_regular = S_ISREG(statbuf.st_mode);
    end.size       = statbuf.st_size;
    end.position   = offset != nullptr ? *offset : syscall_no_intercept(SYS_lseek, fd, 0, SEEK_CUR);
    LOG("fd %d is a posix fd: flags=%d, is_dir=%s, size=%ld, position=%ld", fd, end.flags,
        end.is_dir ? "true" : "false", end.size, end.position);
    return end;
}

/**
 * Move the CAPIO descriptor of @end to the position of the next read or write. The two ends may
 * share the descriptor offset, as for the same descriptor or a duplicate, so the offset is set
 * before each read and write and not only once.
 *
 * @param end
 * @param tid
 */
inline void capio_copy_file_range_seek(const CPCopyFileRangeEnd_t &end, const long tid) {
    if (get_capio_fd_offset(end.fd) != end.position) {
        capio_set_fd_offset(end.fd, end.position, tid);
    }
}

/**
 * Read up to @count bytes from @end into @buffer.
 *
 * @param end
 * @param buffer
 * @param count
 * @param tid
 * @return the number of bytes read, 0 at the end of the file, or -errno
 */
inline off64_t capio_copy_file_range_read(CPCopyFileRangeEnd_t &end, char *buffer,
                                          const off64_t count, const long tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", end.fd, count);

    if (end.is_capio) {
        capio_copy_file_range_seek(end, tid);
        const off64_t bytes_read = capio_cached_read(end.fd, buffer, count);
        end.position += bytes_read;
        return bytes_read;
    }
    if (end.offset != nullptr) {
        const long bytes_read =
            syscall_no_intercept(SYS_pread64, end.fd, buffer, count, end.position);
        if (bytes_read > 0) {
            end.position += bytes_read;
        }
        return bytes_read;
    }
    return syscall_no_intercept(SYS_read, end.fd, buffer, count);
}

/**
 * Write up to @count bytes of @buffer to @end.
 *
 * @param end
 * @param buffer
 * @param count
 * @param tid
 * @return the number of bytes written, or -errno
 */
inline off64_t capio_copy_file_range_write(CPCopyFileRangeEnd_t &end, const char *buffer,
                                           const off64_t count, const long tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", end.fd, count);

    if (end.is_capio) {
        capio_copy_file_range_seek(end, tid);
        capio_cached_write(end.fd, buffer, count);
        end.position += count;
        return count;
    }
    if (end.offset != nullptr) {
        const long bytes_written =
            syscall_no_intercept(SYS_pwrite64, end.fd, buffer, count, end.position);
        if (bytes_written > 0) {
            end.position += bytes_written;
        }
        return bytes_written;
    }
    return syscall_no_intercept(SYS_write, end.fd, buffer, count);
}

/**
 * Write all the @count bytes of @buffer to @end, writing the rest again after a short write.
 *
 * @param end
 * @param buffer
 * @param count
 * @param error set to -errno when a write fails
 * @param tid
 * @return the number of bytes written: @count, or fewer if a write failed
 */
inline off64_t capio_copy_file_range_write_all(CPCopyFileRangeEnd_t &end, const char *buffer,
                                               const off64_t count, off64_t &error,
                                               const long tid) {
    START_LOG(tid, "call(fd=%d, count=%ld)", end.fd, count);

    off64_t bytes_written = 0;
    while (bytes_written < count) {
        const off64_t res =
            capio_copy_file_range_write(end, buffer + bytes_written, count - bytes_written, tid);
        if (res <= 0) {
            LOG("write returned %ld after %ld bytes", res, bytes_written);
            // a write of 0 bytes makes no progress: it is reported as an I/O error
            error = res == 0 ? -EIO : res;
            break;
        }
        bytes_written += res;
    }
    return bytes_written;
}

/**
 * Give back to the input the @count bytes it was read past the data copied, when a write fails
 * halfway: the next copy starts from the first byte not copied.
 *
 * @param end
 * @param count
 */
inline void capio_copy_file_range_unread(CPCopyFileRangeEnd_t &end, const off64_t count) {
    if (end.is_capio || end.offset != nullptr) {
        end.position -= count;
    } else {
        syscall_no_intercept(SYS_lseek, end.fd, -count, SEEK_CUR);
    }
}

/**
 * Set the offsets of @end once the copy is done: the offset passed by the caller is advanced by the
 * bytes copied and the CAPIO descriptor offset goes back to where it was, or the CAPIO descriptor
 * offset moves past the bytes copied when no offset was passed.
 *
 * @param end
 * @param copied
 * @param tid
 */
inline void capio_copy_file_range_finish(CPCopyFileRangeEnd_t &end, const off64_t copied,
                                         const long tid) {
    if (end.offset != nullptr) {
        *end.offset += copied;
        if (end.is_capio) {
            end.position = end.saved_fd_offset;
            capio_copy_file_range_seek(end, tid);
        }
    } else if (end.is_capio) {
        capio_copy_file_range_seek(end, tid);
    }
}

/**
 * Copy @count bytes between two CAPIO descriptors on the server: the server copies the data from
 * one file to the other, and the data never comes to the client. The server copies up to the end
 * of the data that follows the input position at each request, so the client asks until @count
 * bytes are copied or the input ends.
 *
 * @param in
 * @param out
 * @param count
 * @param tid
 * @return the number of bytes copied
 */
inline off64_t capio_copy_file_range_on_server(CPCopyFileRangeEnd_t &in, CPCopyFileRangeEnd_t &out,
                                               const off64_t count, const long tid) {
    START_LOG(tid, "call(fd_in=%d, fd_out=%d, count=%ld)", in.fd, out.fd, count);

    // the server copies the data it holds: the data this thread wrote must reach it first, and the
    // read cache must not keep data of the output that the copy changes
    write_cache->flush();
    read_cache->flush();

    off64_t copied = 0;
    while (copied < count) {
        const off64_t bytes_copied =
            copy_file_range_request(in.fd, out.fd, in.position, out.position, count - copied, tid);
        if (bytes_copied == 0) {
            LOG("end of fd_in reached");
            break;
        }
        copied += bytes_copied;
        in.position += bytes_copied;
        out.position += bytes_copied;
    }
    return copied;
}

/**
 * Copy @count bytes between a CAPIO descriptor and a descriptor of the kernel, through a buffer of
 * one CAPIO cache line: each chunk is read from @in and written to @out, until @count bytes are
 * copied, the input ends, or a read or a write fails. A chunk of one line fills one line of the
 * read cache and one line of the write cache, as a read and write loop of the same size would.
 *
 * @param in
 * @param out
 * @param count
 * @param error set to -errno when a read or a write fails, or the buffer cannot be mapped
 * @param tid
 * @return the number of bytes copied
 */
inline off64_t capio_copy_file_range_through_buffer(CPCopyFileRangeEnd_t &in,
                                                    CPCopyFileRangeEnd_t &out, const off64_t count,
                                                    off64_t &error, const long tid) {
    START_LOG(tid, "call(fd_in=%d, fd_out=%d, count=%ld)", in.fd, out.fd, count);

    // TODO: read and write CAPIO data in place in the shared memory caches, to avoid the copy into
    //  and out of this buffer
    const off64_t chunk_size = std::min<off64_t>(count, get_cache_line_size());

    // the buffer is not allocated with new or malloc: for a buffer this large, malloc issues mmap
    // and munmap itself, and those system calls would be intercepted again by CAPIO
    const long mapping = syscall_no_intercept(SYS_mmap, nullptr, chunk_size, PROT_READ | PROT_WRITE,
                                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (syscall_error_code(mapping) != 0) {
        LOG("cannot map a buffer of %ld bytes", chunk_size);
        error = -ENOMEM;
        return 0;
    }
    auto *buffer   = reinterpret_cast<char *>(mapping);
    off64_t copied = 0;
    while (copied < count) {
        const off64_t bytes_read =
            capio_copy_file_range_read(in, buffer, std::min(count - copied, chunk_size), tid);
        if (bytes_read == 0) {
            LOG("end of fd_in reached");
            break;
        }
        if (bytes_read < 0) {
            LOG("read from fd_in failed with %ld", bytes_read);
            error = bytes_read;
            break;
        }

        const off64_t bytes_written =
            capio_copy_file_range_write_all(out, buffer, bytes_read, error, tid);
        copied += bytes_written;
        if (bytes_written < bytes_read) {
            // the bytes read but not written are not copied: the input goes back to the first one
            capio_copy_file_range_unread(in, bytes_read - bytes_written);
            break;
        }
    }
    syscall_no_intercept(SYS_munmap, buffer, chunk_size);
    return copied;
}

inline off64_t capio_copy_file_range(const int fd_in, loff_t *off_in, const int fd_out,
                                     loff_t *off_out, const size_t len, const unsigned int flags,
                                     const long tid) {
    START_LOG(tid, "call(fd_in=%d, off_in=0x%08x, fd_out=%d, off_out=0x%08x, len=%ld, flags=%u)",
              fd_in, off_in, fd_out, off_out, len, flags);

    if (!exists_capio_fd(fd_in) && !exists_capio_fd(fd_out)) {
        LOG("neither fd_in nor fd_out is a capio fd. skipping");
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    if (flags != 0) {
        LOG("flags must be 0");
        errno = EINVAL;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    CPCopyFileRangeEnd_t in = capio_copy_file_range_end(fd_in, off_in, tid);
    if (!in.is_open) {
        errno = EBADF;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    CPCopyFileRangeEnd_t out = capio_copy_file_range_end(fd_out, off_out, tid);
    if (!out.is_open) {
        errno = EBADF;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    if (in.is_dir || out.is_dir) {
        LOG("fd_in or fd_out is a directory");
        errno = EISDIR;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    if (!in.is_regular || !out.is_regular) {
        LOG("fd_in or fd_out is not a regular file");
        errno = EINVAL;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    if (!in.isReadable()) {
        LOG("fd_in is not open for reading");
        errno = EBADF;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    if (!out.isWritable() || (out.flags & O_APPEND) != 0) {
        LOG("fd_out is not open for writing, or is open in append mode");
        errno = EBADF;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    // fail with EOVERFLOW if a range [position, position + len) wraps past the largest 64-bit
    // offset just like in the kernel.
    const auto in_start = static_cast<uint64_t>(in.position);
    if (const auto out_start = static_cast<uint64_t>(out.position);
        in_start + len < in_start || out_start + len < out_start) {
        LOG("the range of fd_in at %ld or of fd_out at %ld overflows", in.position, out.position);
        errno = EOVERFLOW;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    // the kernel checks for negative offsets after the overlap below, but both fail with EINVAL:
    // the cheaper check goes first
    if (in.position < 0 || out.position < 0) {
        LOG("the offset of fd_in (%ld) or of fd_out (%ld) is negative", in.position, out.position);
        errno = EINVAL;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    // an empty copy still fails on everything checked above, as in the kernel, but the overlap
    // check below cannot fail with empty ranges
    if (len == 0) {
        LOG("nothing to copy");
        return 0;
    }

    // copying a range of a file onto itself is refused, as in the kernel. Only the data that exists
    // when the copy starts counts: the size is not used to shorten the copy, because a CAPIO file
    // may still be growing, and reading it waits for its data.
    // Only two CAPIO descriptors can name the same file: a path is handled either by CAPIO or by
    // the kernel. Both checks are also needed because get_capio_fd_path() throws on a kernel fd.
    if (in.is_capio && out.is_capio && get_capio_fd_path(in.fd) == get_capio_fd_path(out.fd)) {

        const auto overlap_count = static_cast<off64_t>(
            in.position >= in.size ? 0 : std::min<uint64_t>(len, in.size - in.position));

        if (out.position + overlap_count > in.position &&
            out.position < in.position + overlap_count) {
            LOG("source and destination ranges of the same file overlap");
            errno = EINVAL;
            return CAPIO_POSIX_SYSCALL_ERRNO;
        }
    }

    // as in the kernel, a single call copies at most capio_max_rw_count() bytes
    const auto count = static_cast<off64_t>(std::min<size_t>(len, capio_max_rw_count()));

    off64_t error        = 0;
    const off64_t copied = in.is_capio && out.is_capio
                               ? capio_copy_file_range_on_server(in, out, count, tid)
                               : capio_copy_file_range_through_buffer(in, out, count, error, tid);
    LOG("copied %ld bytes", copied);

    // when the two ends share the descriptor offset, the end without an offset passed by the caller
    // moves it, so it is finished last
    if (in.offset == nullptr) {
        capio_copy_file_range_finish(out, copied, tid);
        capio_copy_file_range_finish(in, copied, tid);
    } else {
        capio_copy_file_range_finish(in, copied, tid);
        capio_copy_file_range_finish(out, copied, tid);
    }

    // as in the kernel, an error is reported only when no byte was copied
    if (copied == 0 && error < 0) {
        errno = static_cast<int>(-error);
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    return copied;
}

inline int copy_file_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                                   long *result) {
    const auto fd_in  = static_cast<int>(arg0);
    auto *off_in      = reinterpret_cast<loff_t *>(arg1);
    const auto fd_out = static_cast<int>(arg2);
    auto *off_out     = reinterpret_cast<loff_t *>(arg3);
    const auto len    = static_cast<size_t>(arg4);
    const auto flags  = static_cast<unsigned int>(arg5);
    const long tid    = syscall_no_intercept(SYS_gettid);

    return posix_return_value(
        capio_copy_file_range(fd_in, off_in, fd_out, off_out, len, flags, tid), result);
}

#endif // SYS_copy_file_range
#endif // CAPIO_POSIX_HANDLERS_COPY_FILE_RANGE_HPP

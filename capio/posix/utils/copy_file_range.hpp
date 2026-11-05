#ifndef CAPIO_POSIX_UTILS_COPY_FILE_RANGE_HPP
#define CAPIO_POSIX_UTILS_COPY_FILE_RANGE_HPP

/**
 * How copy_file_range moves data when a descriptor is a CAPIO one: through a buffer of the client,
 * with the reads and writes of the CAPIO caches for a CAPIO end, and pread, pwrite, read and write
 * for a descriptor of the kernel.
 */

#include <sys/mman.h>
#include <sys/stat.h>

#include "common/env.hpp"

#include "data.hpp"
#include "filesystem.hpp"
#include "requests.hpp"
#include "types.hpp"

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
 * Copy @count bytes from @in to @out, at least one of them a CAPIO descriptor, through a buffer of
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

#endif // CAPIO_POSIX_UTILS_COPY_FILE_RANGE_HPP

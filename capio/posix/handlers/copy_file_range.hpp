#ifndef CAPIO_POSIX_HANDLERS_COPY_FILE_RANGE_HPP
#define CAPIO_POSIX_HANDLERS_COPY_FILE_RANGE_HPP

#if defined(SYS_copy_file_range)

#include "utils/common.hpp"
#include "utils/copy_file_range.hpp"
#include "utils/filesystem.hpp"

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

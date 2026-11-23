#ifndef CAPIO_POSIX_UTILS_FS_WAITING_HPP
#define CAPIO_POSIX_UTILS_FS_WAITING_HPP

#include <climits>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "cache.hpp"
#include "filesystem.hpp"

/*
 * read and readv of the FS variant wait until the kernel has the data to hand out, then the
 * kernel runs them: until the first @end bytes (offset + count) can be read. In update mode, or for
 * an end of FS_UNTIL_COMMIT, that is the commit. In no_update mode it is @end bytes on the file
 * system, or the commit.
 *
 * stat and fstat never wait for data: in no_update they return the current size.
 */
constexpr off64_t FS_UNTIL_COMMIT = LLONG_MAX;

/// @brief Wait until the first @p end bytes of @p fd can be read
inline void fs_wait_for_data(const int fd, const off64_t end, const long tid) {
    const std::string path = get_capio_fd_path(fd);
    if (path.empty()) {
        return;
    }
    if (read_request_cache->covers(path, end)) {
        return;
    }
    const long flags = syscall_no_intercept(SYS_fcntl, fd, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY) {
        return; // a writer: fopen(..., "a") seeks to the end
    }
    struct stat info{};
    if (syscall_no_intercept(SYS_fstat, fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        return;
    }
    read_request_cache->read_request(path, end, tid, fd);

#ifdef SYS_statx
    // NFS may still have the old size cached, and would answer end of file
    struct statx fresh{};
    syscall_no_intercept(SYS_statx, fd, "", AT_EMPTY_PATH | AT_STATX_FORCE_SYNC, STATX_SIZE,
                         &fresh);
#endif
}

/// @brief Where a read happens: at @p offset if the caller gave one, or at the file offset
inline off64_t fs_read_position(const int fd, const off64_t *offset) {
    if (offset != nullptr) {
        return *offset;
    }
    const long position = syscall_no_intercept(SYS_lseek, fd, 0, SEEK_CUR);
    return position < 0 ? 0 : position;
}

inline off64_t fs_end_of(const off64_t position, const off64_t count) {
    return count > FS_UNTIL_COMMIT - position ? FS_UNTIL_COMMIT : position + count;
}

inline off64_t fs_total_length(const struct iovec *iov, const int iovcnt) {
    off64_t total = 0;
    for (int i = 0; i < iovcnt; ++i) {
        total = fs_end_of(total, static_cast<off64_t>(iov[i].iov_len));
    }
    return total;
}

#endif // CAPIO_POSIX_UTILS_FS_WAITING_HPP

#ifndef CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP
#define CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP

#if !CAPIO_MEM_STREAM

/*
 * Handlers that the FS variant uses in place of the memory ones (see build_syscall_table). Files
 * are real, so the kernel runs the call: these only wait for data (utils/fs_waiting.hpp), keep
 * track of descriptors and tell the server (utils/fs_descriptors.hpp).
 */

#include <linux/fs.h>
#include <sys/mman.h>

#include "utils/fs_descriptors.hpp"
#include "utils/fs_listing.hpp"
#include "utils/fs_waiting.hpp"

/* Calls that hand out data: wait for it, then the kernel runs them */

int read_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        fs_wait_for_data(fd, fs_end_of(fs_read_position(fd, nullptr), arg2),
                         syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int readv_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        const auto *iov = reinterpret_cast<const struct iovec *>(arg1);
        fs_wait_for_data(
            fd,
            fs_end_of(fs_read_position(fd, nullptr), fs_total_length(iov, static_cast<int>(arg2))),
            syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#ifdef SYS_pread64
int pread_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg0);
    if (arg3 >= 0 && exists_capio_fd(fd)) {
        fs_wait_for_data(fd, fs_end_of(arg3, arg2), syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

#if defined(SYS_preadv) || defined(SYS_preadv2)
int preadv_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg0);
    // preadv2 takes -1 for the file offset
    if (arg3 >= -1 && exists_capio_fd(fd)) {
        const off64_t position = arg3 == -1 ? fs_read_position(fd, nullptr) : arg3;
        const auto *iov        = reinterpret_cast<const struct iovec *>(arg1);
        fs_wait_for_data(fd, fs_end_of(position, fs_total_length(iov, static_cast<int>(arg2))),
                         syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

/// @brief copy_file_range, sendfile and splice: they may return less, but 0 only at the commit
inline int fs_wait_to_copy(const int fd_in, const long offset_pointer, const long count) {
    if (count != 0 && exists_capio_fd(fd_in)) {
        const auto position =
            fs_read_position(fd_in, reinterpret_cast<const off64_t *>(offset_pointer));
        fs_wait_for_data(fd_in, fs_end_of(position, 1), syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int copy_file_range_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                               long *result) {
    return fs_wait_to_copy(static_cast<int>(arg0), arg1, arg4);
}

int sendfile_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    return fs_wait_to_copy(static_cast<int>(arg1), arg2, arg3); // (out, in, offset, count)
}

int splice_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    return fs_wait_to_copy(static_cast<int>(arg0), arg1, arg4);
}

#ifdef SYS_mmap
int mmap_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    const int fd = static_cast<int>(arg4);
    if (exists_capio_fd(fd)) {
        fs_wait_for_data(fd, FS_UNTIL_COMMIT, syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}
#endif

/// @brief FICLONE(dest, src) is an instant snapshot of src: wait for its commit
int ioctl_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const auto request = static_cast<unsigned long>(arg1);
    int source         = -1;
    if (request == FICLONE) {
        source = static_cast<int>(arg2);
    } else if (request == FICLONERANGE && arg2 != 0) {
        source = static_cast<int>(reinterpret_cast<const struct file_clone_range *>(arg2)->src_fd);
    }
    if (source >= 0 && exists_capio_fd(source)) {
        fs_wait_for_data(source, FS_UNTIL_COMMIT, syscall_no_intercept(SYS_gettid));
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int lseek_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd      = static_cast<int>(arg0);
    const auto whence = static_cast<int>(arg2);
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    if (fs_seek_listing(fd, arg1, whence, *result)) {
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    const long tid            = syscall_no_intercept(SYS_gettid);
    const bool looks_for_data = whence == SEEK_DATA || whence == SEEK_HOLE;
    if (whence == SEEK_END) {
        fs_wait_for_data(fd, FS_UNTIL_COMMIT, tid);
    } else if (looks_for_data && arg1 >= 0) {
        fs_wait_for_data(fd, fs_end_of(arg1, 1), tid);
    }
    *result = syscall_no_intercept(SYS_lseek, fd, arg1, whence);
    if (*result == -ENXIO && looks_for_data) {
        // Only a hole so far. Whether data follows is known at the commit.
        fs_wait_for_data(fd, FS_UNTIL_COMMIT, tid);
        *result = syscall_no_intercept(SYS_lseek, fd, arg1, whence);
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

int getdents_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    return fs_getdents(static_cast<int>(arg0), reinterpret_cast<char *>(arg1),
                       static_cast<size_t>(arg2), false, syscall_no_intercept(SYS_gettid), result);
}

int getdents64_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    return fs_getdents(static_cast<int>(arg0), reinterpret_cast<char *>(arg1),
                       static_cast<size_t>(arg2), true, syscall_no_intercept(SYS_gettid), result);
}

/* Calls on a descriptor that only wait for the path, like stat */

int fd_consent_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    const int fd = static_cast<int>(arg0);
    if (exists_capio_fd(fd)) {
        consent_request_cache->consent_request(get_capio_fd_path(fd),
                                               syscall_no_intercept(SYS_gettid), __FUNCTION__);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

/* Descriptors: the kernel acts, then CAPIO tracks and announces */

int close_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    return fs_close(fd, syscall_no_intercept(SYS_gettid), result);
}

#ifdef SYS_close_range
int close_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    return fs_close_range(static_cast<unsigned>(arg0), static_cast<unsigned>(arg1),
                          static_cast<unsigned>(arg2), syscall_no_intercept(SYS_gettid), result);
}
#endif

int dup_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    return fs_dup(static_cast<int>(arg0), syscall_no_intercept(SYS_gettid), result);
}

int dup2_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    return fs_dup_onto(static_cast<int>(arg0), static_cast<int>(arg1), 0, false,
                       syscall_no_intercept(SYS_gettid), result);
}

int dup3_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    return fs_dup_onto(static_cast<int>(arg0), static_cast<int>(arg1), static_cast<int>(arg2), true,
                       syscall_no_intercept(SYS_gettid), result);
}

int fcntl_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                     long *result) {
    const int fd = static_cast<int>(arg0);
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    return fs_fcntl(fd, static_cast<int>(arg1), arg2, syscall_no_intercept(SYS_gettid), result);
}

int execve_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                      long *result) {
    return fs_execve(arg0, arg1, arg2, syscall_no_intercept(SYS_gettid), result);
}

/// @brief The end of a thread closes nothing: only the last one is the exit of the process
int exit_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                    long *result) {
    const long tid = syscall_no_intercept(SYS_gettid);
    if (fs_is_last_thread()) {
        fs_process_exit(tid);
    }
    exit_group_request(tid);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

int exit_group_fs_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                          long *result) {
    const long tid = syscall_no_intercept(SYS_gettid);
    fs_process_exit(tid);
    exit_group_request(tid);
    return CAPIO_POSIX_SYSCALL_SKIP;
}

#endif // !CAPIO_MEM_STREAM
#endif // CAPIO_POSIX_HANDLERS_FS_HANDLERS_HPP

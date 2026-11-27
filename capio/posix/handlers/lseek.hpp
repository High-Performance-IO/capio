#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#if defined(SYS_lseek)

#include "handlers/read.hpp"
#include "utils/common.hpp"
#include "utils/data.hpp"

/// @brief @p base + @p offset, or -1 if it overflows: Linux fails such a seek with EINVAL, as one
/// before the start of the file
inline off64_t capio_seek_target(const off64_t base, const off64_t offset) {
    off64_t target;
    return __builtin_add_overflow(base, offset, &target) ? -1 : target;
}

inline off64_t capio_lseek(int fd, off64_t offset, int whence, long tid) {
    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);

    if (!fd_table->contains(fd)) {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }

    MEMORY_MODE({
        read_cache->flush();
        write_cache->flush();
        off64_t file_offset = fd_table->offset(fd);
        if (whence == SEEK_SET) {
            LOG("whence %d is SEEK_SET", whence);
            if (offset >= 0) {
                capio_set_fd_offset(fd, offset, tid);
                LOG("the new offset of file %d is %ld", fd, offset);
                return offset;
            } else {
                errno = EINVAL;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else if (whence == SEEK_CUR) {
            LOG("whence %d is SEEK_CUR", whence);
            off64_t new_offset = capio_seek_target(file_offset, offset);
            if (new_offset >= 0) {
                capio_set_fd_offset(fd, new_offset, tid);
                LOG("the new offset of file %d is %ld", fd, new_offset);
                return new_offset;
            } else {
                errno = EINVAL;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
        } else if (whence == SEEK_END) {
            LOG("whence %d is SEEK_END", whence);
            off64_t new_offset = capio_seek_target(seek_end_request(fd, tid), offset);
            if (new_offset < 0) {
                errno = EINVAL;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
            capio_set_fd_offset(fd, new_offset, tid);
            LOG("the new offset of file %d is %ld", fd, new_offset);
            return new_offset;
        } else if (whence == SEEK_DATA) {
            LOG("whence %d is SEEK_DATA", whence);
            off64_t new_offset = seek_data_request(fd, file_offset, tid);
            fd_table->setOffset(fd, new_offset);
            LOG("the new offset of file %d is %ld", fd, new_offset);
            return new_offset;
        } else if (whence == SEEK_HOLE) {
            LOG("whence %d is SEEK_HOLE", whence);
            off64_t new_offset = seek_hole_request(fd, file_offset, tid);
            fd_table->setOffset(fd, new_offset);
            LOG("the new offset of file %d is %ld", fd, new_offset);
            return new_offset;
        } else {
            LOG("whence %d is invalid", whence);
            errno = EINVAL;
            return CAPIO_POSIX_SYSCALL_ERRNO;
        }
    })
    FS_MODE({
        // A directory that the server lists (see getdents) is sent the offset: a seek, as
        // rewinddir and seekdir make, only moves it. telldir reads it back.
        if ((fd_table->flags(fd) & O_DIRECTORY) != 0 && !read_request_cache->isListedByKernel(fd)) {
            const off64_t new_offset = whence == SEEK_SET ? offset
                                       : whence == SEEK_CUR
                                           ? capio_seek_target(fd_table->offset(fd), offset)
                                           : -1;
            if (new_offset < 0) {
                errno = EINVAL;
                return CAPIO_POSIX_SYSCALL_ERRNO;
            }
            fd_table->setOffset(fd, new_offset);
            return new_offset;
        }
        // The end of a file and its data are known only once the producer wrote them, so a reader
        // waits for them first. The other whence values only move the offset of a real descriptor.
        const bool looks_for_data = whence == SEEK_DATA || whence == SEEK_HOLE;
        if (whence == SEEK_END) {
            capio_wait_for_data_fs(fd, 0, CAPIO_UNTIL_COMMIT_FS, tid);
        } else if (looks_for_data && offset >= 0) {
            capio_wait_for_data_fs(fd, offset, 1, tid);
        }
        if (!looks_for_data) {
            return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
        }
        long res = syscall_no_intercept(SYS_lseek, fd, offset, whence);
        if (res == -ENXIO) {
            // Only a hole so far. Whether data follows is known at the commit.
            capio_wait_for_data_fs(fd, 0, CAPIO_UNTIL_COMMIT_FS, tid);
            res = syscall_no_intercept(SYS_lseek, fd, offset, whence);
        }
        return capio_kernel_result(res);
    })
}

int lseek_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd      = static_cast<int>(arg0);
    auto offset = static_cast<off64_t>(arg1);
    int whence  = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_lseek(fd, offset, whence, tid), result);
}

#endif // SYS_lseek
#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP

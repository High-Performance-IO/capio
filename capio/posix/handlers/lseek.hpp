#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#if defined(SYS_lseek)

#include "handlers/getdents.hpp"
#include "utils/common.hpp"
#include "utils/data.hpp"

// TODO: EOVERFLOW is not addressed
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
            off64_t new_offset = file_offset + offset;
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
            off64_t new_offset = seek_end_request(fd, tid) + offset;
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
        if (long res; capio_seek_listing_fs(fd, offset, whence, res)) {
            return capio_kernel_result(res); // a directory that CAPIO is listing
        }
        // The end of a file and its data are known only once the producer wrote them, so a reader
        // must wait for them first. The writer knows its own, and the other whence values only
        // move the offset of a real descriptor: the kernel runs them.
        const bool to_data = whence == SEEK_END || whence == SEEK_DATA || whence == SEEK_HOLE;
        if (to_data && (fd_table->flags(fd) & O_ACCMODE) == O_RDONLY) {
            throw CapioUnimplementedException("lseek of a reader to the end, data or a hole");
        }
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
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

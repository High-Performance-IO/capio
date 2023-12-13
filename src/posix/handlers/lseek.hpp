#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#include "utils/functions.hpp"

// TODO: EOVERFLOW is not addressed
inline off64_t capio_lseek(int fd, off64_t offset, int whence, long tid) {
    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);

    if (exists_capio_fd(fd)) {
        off64_t file_offset = get_capio_fd_offset(fd);
        if (whence == SEEK_SET) {
            if (offset >= 0) {
                set_capio_fd_offset(fd, offset);
                seek_request(fd, offset, tid);
                return offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_CUR) {
            off64_t new_offset = file_offset + offset;
            if (new_offset >= 0) {
                set_capio_fd_offset(fd, new_offset);
                seek_request(fd, new_offset, tid);
                return new_offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_END) {
            off64_t file_size  = seek_end_request(fd, tid);
            off64_t new_offset = file_offset + offset;
            set_capio_fd_offset(fd, new_offset);
            return new_offset;
        } else if (whence == SEEK_DATA) {
            off64_t new_offset = seek_data_request(fd, file_offset, tid);
            set_capio_fd_offset(fd, new_offset);
            return new_offset;
        } else if (whence == SEEK_HOLE) {
            off64_t new_offset = seek_hole_request(fd, file_offset, tid);
            set_capio_fd_offset(fd, new_offset);
            return new_offset;
        } else {
            errno = EINVAL;
            return -1;
        }
    } else {
        return -2;
    }
}

int lseek_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd      = static_cast<int>(arg0);
    auto offset = static_cast<off64_t>(arg1);
    int whence  = static_cast<int>(arg2);
    long tid    = syscall_no_intercept(SYS_gettid);

    return posix_return_value(capio_lseek(fd, offset, whence, tid), result);
}

#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP

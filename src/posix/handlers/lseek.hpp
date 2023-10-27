#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#include "globals.hpp"

// TODO: EOVERFLOW is not adressed
inline off64_t capio_lseek(int fd, off64_t offset, int whence, long tid) {
    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);

    auto it = files->find(fd);
    if (it != files->end()) {
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *file_offset                          = std::get<0>(*t);
        if (whence == SEEK_SET) {
            if (offset >= 0) {
                *file_offset = offset;
                seek_request(fd, *file_offset, tid);

                return *file_offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_CUR) {
            off64_t new_offset = *file_offset + offset;
            if (new_offset >= 0) {
                *file_offset = new_offset;
                seek_request(fd, *file_offset, tid);

                return *file_offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_END) {
            off64_t file_size = seek_end_request(fd, tid);
            *file_offset      = file_size + offset;

            return *file_offset;
        } else if (whence == SEEK_DATA) {
            *file_offset = seek_data_request(fd, *file_offset, tid);

            return *file_offset;
        } else if (whence == SEEK_HOLE) {
            *file_offset = seek_hole_request(fd, *file_offset, tid);

            return *file_offset;
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
    START_LOG(tid, "call(fd=%d, offset=%ld, whence=%d)", fd, offset, whence);

    off64_t res = capio_lseek(fd, offset, whence, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP

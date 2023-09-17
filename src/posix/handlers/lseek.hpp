#ifndef CAPIO_POSIX_HANDLERS_LSEEK_HPP
#define CAPIO_POSIX_HANDLERS_LSEEK_HPP

#include "globals.hpp"

//TODO: EOVERFLOW is not adressed
inline off64_t capio_lseek(int fd, off64_t offset,int whence, long tid) {

    CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: enter\n", tid, fd, offset, whence);

    auto it = files->find(fd);
    if (it != files->end()) {
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *file_offset = std::get<0>(*t);
        if (whence == SEEK_SET) {

            if (offset >= 0) {
                *file_offset = offset;
                seek_request(fd, *file_offset, tid);
                CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_SET with non negative offset, return %ld\n", tid, fd, offset, whence, *file_offset);
                return *file_offset;
            } else {
                errno = EINVAL;
                CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_SET with negative offset, return -1\n", tid, fd, offset, whence);
                return -1;
            }
        } else if (whence == SEEK_CUR) {

            off64_t new_offset = *file_offset + offset;
            if (new_offset >= 0) {
                *file_offset = new_offset;
                seek_request(fd, *file_offset, tid);
                CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_CUR with non negative offset, return %ld\n", tid, fd, offset, whence, *file_offset);
                return *file_offset;
            } else {
                errno = EINVAL;
                CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_CUR with negative offset, return -1\n", tid, fd, offset, whence);
                return -1;
            }
        } else if (whence == SEEK_END) {

            off64_t file_size = seek_end_request(fd, tid);
            *file_offset = file_size + offset;
            CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_CUR, return %ld\n", tid, fd, offset, whence, *file_offset);
            return *file_offset;
        } else if (whence == SEEK_DATA) {

            *file_offset = seek_data_request(fd, *file_offset, tid);
            CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_DATA, return %ld\n", tid, fd, offset, whence, *file_offset);
            return *file_offset;

        } else if (whence == SEEK_HOLE) {

            *file_offset = seek_hole_request(fd, *file_offset, tid);
            CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: SEEK_HOLE, return %ld\n", tid, fd, offset, whence, *file_offset);
            return *file_offset;

        } else {
            errno = EINVAL;
            CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: invalid whence value, return -1\n", tid, fd, offset, whence);
            return -1;
        }

    } else {
        CAPIO_DBG("capio_lseek TID[%ld] FD[%d] OFFSET[%ld] WHENCE[%d]: external file, return -2\n", tid, fd, offset, whence);
        return -2;
    }
}


int lseek_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long tid){

    int fd = static_cast<int>(arg0);
    auto offset = static_cast<off64_t>(arg1);
    int whence = static_cast<int>(arg2);
    off64_t res = capio_lseek(fd, offset, whence, tid);

    CAPIO_DBG("seek tid: %d fd: %d offset: %ld res: %ld\n", syscall_no_intercept(SYS_gettid), fd, offset, res);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_LSEEK_HPP

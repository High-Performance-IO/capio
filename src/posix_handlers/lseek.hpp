#ifndef CAPIO_LSEEK_HPP
#define CAPIO_LSEEK_HPP

//TODO: EOVERFLOW is not adressed
off_t capio_lseek(int fd,off64_t offset,int whence) {
    long my_tid =  syscall_no_intercept(SYS_gettid);
    auto it = files->find(fd);
    char c_str[256];
    if (it != files->end()) {
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *file_offset = std::get<0>(*t);
        if (whence == SEEK_SET) {

            CAPIO_DBG("capio seek set\n");

            if (offset >= 0) {
                *file_offset = offset;
                sprintf(c_str, "seek %ld %d %zu",my_tid, fd, *file_offset);
                buf_requests->write(c_str, 256 * sizeof(char));
                off64_t offset_upperbound;
                (*bufs_response)[my_tid]->read(&offset_upperbound);
                return *file_offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_CUR) {

            CAPIO_DBG("capio seek curr\n");

            off64_t new_offset = *file_offset + offset;
            if (new_offset >= 0) {
                *file_offset = new_offset;
                sprintf(c_str, "seek %ld %d %zu", my_tid, fd, *file_offset);
                buf_requests->write(c_str, 256 * sizeof(char));
                off64_t offset_upperbound;
                (*bufs_response)[my_tid]->read(&offset_upperbound);
                return *file_offset;
            } else {
                errno = EINVAL;
                return -1;
            }
        } else if (whence == SEEK_END) {

            CAPIO_DBG("capio seek end\n");

            off64_t file_size;
            sprintf(c_str, "send %ld %d", my_tid, fd);
            buf_requests->write(c_str, 256 * sizeof(char));
            (*bufs_response)[my_tid]->read(&file_size);
            *file_offset = file_size + offset;
            return *file_offset;
        } else if (whence == SEEK_DATA) {

            CAPIO_DBG("capio seek data\n");

            char c_str[64];
            sprintf(c_str, "sdat %ld %d %zu", my_tid, fd, *file_offset);
            buf_requests->write(c_str, 256 * sizeof(char));
            (*bufs_response)[my_tid]->read(file_offset);
            return *file_offset;

        } else if (whence == SEEK_HOLE) {

            CAPIO_DBG("capio seek hole\n");

            char c_str[64];
            sprintf(c_str, "shol %ld %d %zu",my_tid, fd, *file_offset);
            buf_requests->write(c_str, 256 * sizeof(char));

            CAPIO_DBG("capio seek hole debug 1\n");

            (*bufs_response)[my_tid]->read(file_offset);

            CAPIO_DBG("capio seek hole debug2\n");

            return *file_offset;
        } else {
            errno = EINVAL;
            return -1;
        }

    } else {
        return -2;
    }
}


int lseek_handler(long arg0, long arg1, long arg2,  long arg3, long arg4, long arg5, long* result, long my_tid){

    int fd = static_cast<int>(arg0);
    off_t offset = static_cast<off_t>(arg1);
    int whence = static_cast<int>(arg2);
    int res = capio_lseek(fd, offset, whence);

    CAPIO_DBG("seek tid: %d fd: %d offset: %ld res: %ld\n", syscall_no_intercept(SYS_gettid), fd, offset, res);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_LSEEK_HPP

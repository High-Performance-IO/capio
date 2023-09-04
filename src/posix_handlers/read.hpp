#ifndef CAPIO_READ_HPP
#define CAPIO_READ_HPP

off64_t add_read_request(int fd,off64_t count,std::tuple<off64_t *, off64_t *, int, int> &t) {
    char c_str[256];
    long my_tid = syscall_no_intercept(SYS_gettid);
    sprintf(c_str, "read %ld %d %ld", my_tid, fd, count);
    buf_requests->write(c_str, 256 * sizeof(char));
    //read response (offest)
    off64_t offset_upperbound;
    (*bufs_response)[my_tid]->read(&offset_upperbound);
    return offset_upperbound;
}

ssize_t capio_read(int fd,void *buffer,size_t count, long my_tid ) {

    CAPIO_DBG("capio_read %d %d %ld\n", syscall_no_intercept(SYS_gettid), fd, count);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (count >= SSIZE_MAX) {
            std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset = std::get<0>(*t);

        off64_t bytes_read;
        off64_t end_of_read;
        CAPIO_DBG("debug 0\n");
        end_of_read = add_read_request(fd, count_off, *t);
        bytes_read = end_of_read - *offset;
        CAPIO_DBG("before read shm bytes_read %ld end_of_read %ld\n", bytes_read, end_of_read);
        read_shm((*threads_data_bufs)[my_tid].second, *offset, buffer, bytes_read);
        CAPIO_DBG("after read shm\n");
        *offset = *offset + bytes_read;
        CAPIO_DBG("capio_read returning  %ld\n", bytes_read);

        return bytes_read;
    } else {
        CAPIO_DBG("capio read ret -2\n");

        return -2;
    }
}

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){


    int fd = static_cast<int>(arg0);
    void *buf = reinterpret_cast<void *>(arg1);
    size_t count = static_cast<size_t>(arg2);
    int res = capio_read(fd, buf, count, my_tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif //CAPIO_READ_HPP

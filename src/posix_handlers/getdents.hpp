#ifndef CAPIO_CAPIO_POSIX_GETEND_HPP
#define CAPIO_CAPIO_POSIX_GETEND_HPP

off64_t add_getdents_request(int fd,off64_t count,std::tuple<off64_t *,off64_t *, int, int> &t,bool is_getdents64,long my_tid) {
    char c_str[256];

    if (is_getdents64)
        sprintf(c_str, "de64 %ld %d %ld", my_tid, fd, count);
    else
        sprintf(c_str, "dent %ld %d %ld", my_tid, fd, count);
    buf_requests->write(c_str, 256 * sizeof(char));
    //read response (offest)
    off64_t offset_upperbound;
    (*bufs_response)[my_tid]->read(&offset_upperbound);
    off64_t end_of_read = *std::get<0>(t) + count;
    if (end_of_read > offset_upperbound)
        end_of_read = offset_upperbound;

    return offset_upperbound;
}

//TODO: too similar to capio_read, refactoring needed
ssize_t capio_getdents(int fd,void *buffer,size_t count,bool is_getdents64,long my_tid) {

    CAPIO_DBG("capio_getdents (is64_version: %d) %d %d %ld\n",is_getdents64, my_tid, fd, count);

    if (files->find(fd) != files->end()) {
        if (count >= SSIZE_MAX) {
            std::cerr << "capio does not support read bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset = std::get<0>(*t);

        off64_t bytes_read;
        off64_t end_of_read;
        end_of_read = add_getdents_request(fd, count_off, *t, is_getdents64, my_tid);
        bytes_read = end_of_read - *offset;
        if (bytes_read > count_off)
            bytes_read = count_off;
        bytes_read = round(bytes_read, is_getdents64);
        read_shm((*threads_data_bufs)[my_tid].second, *offset, buffer, bytes_read);

        *offset = *offset + bytes_read;

        CAPIO_DBG("capio_getdents returning %ld\n", bytes_read);
        return bytes_read;
    } else {
        return -2;
    }
}

static inline int _getdents_handler(long arg0, long arg1, long arg2, long* result, long my_tid, bool is64bit){
    struct linux_dirent *dirp = reinterpret_cast<struct linux_dirent *>(arg1);
    int res = capio_getdents(static_cast<int>(arg0), dirp, static_cast<unsigned int>(arg2), is64bit, my_tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return  0;
    }
    return 1;
}

int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){
    return _getdents_handler(arg0, arg1, arg2, result, my_tid, false);
}

int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long my_tid){

    return _getdents_handler(arg0, arg1, arg2, result, my_tid, true);
}

#endif //CAPIO_CAPIO_POSIX_GETEND_HPP

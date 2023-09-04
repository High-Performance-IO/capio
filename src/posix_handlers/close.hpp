#ifndef CAPIO_CLOSE_HPP
#define CAPIO_CLOSE_HPP


int add_close_request(int fd,long tid) {
    char c_str[256];
    sprintf(c_str, "clos %ld %d", tid, fd);
    buf_requests->write(c_str, 256 * sizeof(char));
    return 0;
}

int capio_close(int fd, long tid) {

    CAPIO_DBG("capio_close %d %d\n", syscall_no_intercept(SYS_gettid), fd);

    auto it = files->find(fd);
    if (it != files->end()) {
        add_close_request(fd, tid);
        capio_files_descriptors->erase(fd);
        files->erase(fd);
        return close(fd);
    } else {
        CAPIO_DBG("capio_close returning -2 %d %d\n", syscall_no_intercept(SYS_gettid), fd);
        return -2;
    }
}


int close_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    int fd = static_cast<int>(arg0);
    int res = capio_close(fd, my_tid);
    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
return 1;
}

#endif //CAPIO_CLOSE_HPP

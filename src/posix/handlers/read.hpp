#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#include "globals.hpp"
#include "utils/shm.hpp"

inline off64_t capio_read(int fd, void *buffer, off64_t count, long tid) {
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buffer, count);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (count >= SSIZE_MAX) {
            ERR_EXIT("src does not support read bigger than SSIZE_MAX yet");
        }
        off64_t count_off = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset = std::get<0>(*t);
        off64_t end_of_read = read_request(fd, count_off, tid);
        off64_t bytes_read = end_of_read - *offset;
        read_shm(tid, (*threads_data_bufs)[tid].second, *offset, buffer, bytes_read);
        *offset = *offset + bytes_read;
        return bytes_read;
    } else {
        return -2;
    }
}

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    int fd = static_cast<int>(arg0);
    void *buf = reinterpret_cast<void *>(arg1);
    auto count = static_cast<off64_t>(arg2);
    long tid = syscall_no_intercept(SYS_gettid);
    START_LOG(tid, "call(fd=%d, buf=0x%08x, count=%ld)", fd, buf, count);

    off64_t res = capio_read(fd, buf, count, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_READ_HPP

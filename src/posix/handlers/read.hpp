#ifndef CAPIO_POSIX_HANDLERS_READ_HPP
#define CAPIO_POSIX_HANDLERS_READ_HPP

#include "globals.hpp"
#include "utils/shm.hpp"

inline off64_t capio_read(int fd,void *buffer, off64_t count, long tid) {

    CAPIO_DBG("capio_read TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: enter\n", tid, fd, buffer, count);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (count >= SSIZE_MAX) {
            std::cerr << "src does not support read bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;
        std::tuple<off64_t *, off64_t *, int, int> *t = &(*files)[fd];
        off64_t *offset = std::get<0>(*t);
        off64_t end_of_read = read_request(fd, count_off, tid);
        off64_t bytes_read = end_of_read - *offset;

        CAPIO_DBG("capio_read TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: before read shm bytes_read %ld end_of_read %ld\n", tid, fd, buffer, count, bytes_read, end_of_read);

        read_shm((*threads_data_bufs)[tid].second, *offset, buffer, bytes_read);

        CAPIO_DBG("capio_read TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: after read shm\n", tid, fd, buffer, count);

        *offset = *offset + bytes_read;

        CAPIO_DBG("capio_read TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: return %ld\n", tid, fd, buffer, count, bytes_read);

        return bytes_read;
    } else {

        CAPIO_DBG("capio_read TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%ld]: external file, return -2\n", tid, fd, buffer, count);

        return -2;
    }
}

int read_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result, long tid){


    int fd = static_cast<int>(arg0);
    void *buf = reinterpret_cast<void *>(arg1);
    auto count = static_cast<off64_t>(arg2);
    off64_t res = capio_read(fd, buf, count, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_READ_HPP

#ifndef CAPIO_POSIX_HANDLERS_WRITE_HPP
#define CAPIO_POSIX_HANDLERS_WRITE_HPP

#include "globals.hpp"
#include "utils/requests.hpp"
#include "utils/shm.hpp"

inline ssize_t capio_write(int fd,const void *buffer,size_t count,long tid) {

    CAPIO_DBG("capio_write TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%d]: enter\n", tid, fd, buffer, count);

    auto it = files->find(fd);
    if (it != files->end()) {
        if (count > SSIZE_MAX) {
            std::cerr << "Capio does not support writes bigger then SSIZE_MAX yet" << std::endl;
            exit(1);
        }
        off64_t count_off = count;

        CAPIO_DBG("capio_write TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%d]: send write request\n");

        write_request(files, fd, count_off, tid); //bottleneck

        CAPIO_DBG("capio_write TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%d]: write shared memory\n");

        write_shm((*threads_data_bufs)[tid].first, *std::get<0>((*files)[fd]), buffer, count_off);

        CAPIO_DBG("capio_write TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%d]: return %d\n", count);

        return count;
    } else {

        CAPIO_DBG("capio_write TID[%d] FD[%d] BUFFER[0x%08x] COUNT[%d]: external file, return -2\n");

        return -2;
    }
}

int write_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long* result,  long tid){

    int fd = static_cast<int>(arg0);
    const void *buf = reinterpret_cast<const void *>(arg1);
    auto count = static_cast<size_t>(arg2);

    ssize_t res = capio_write(fd, buf, count, tid);

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}

#endif // CAPIO_POSIX_HANDLERS_WRITE_HPP

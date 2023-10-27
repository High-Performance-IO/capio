#ifndef CAPIO_POSIX_UTILS_SHM_HPP
#define CAPIO_POSIX_UTILS_SHM_HPP

#include "capio/constants.hpp"
#include "capio/logger.hpp"
#include "capio/shm.hpp"
#include "capio/spsc_queue.hpp"

void read_shm(long tid, SPSC_queue<char> *data_buf, long int offset, void *buffer, off64_t count) {
    START_LOG(tid, "call(%ld, %ld, %ld, %ld)", data_buf, offset, buffer, count);
    size_t n_reads = count / WINDOW_DATA_BUFS;
    size_t r       = count % WINDOW_DATA_BUFS;
    size_t i       = 0;
    while (i < n_reads) {
        data_buf->read((char *)buffer + i * WINDOW_DATA_BUFS);
        ++i;
    }
    if (r) {
        data_buf->read((char *)buffer + i * WINDOW_DATA_BUFS, r);
    }
}

void write_shm(long tid, SPSC_queue<char> *data_buf, size_t offset, const void *buffer,
               off64_t count) {
    START_LOG(tid, "call(%ld, %ld, %ld, %ld)", data_buf, offset, buffer, count);
    size_t n_writes = count / WINDOW_DATA_BUFS;
    size_t r        = count % WINDOW_DATA_BUFS;

    size_t i = 0;
    while (i < n_writes) {
        data_buf->write((char *)buffer + i * WINDOW_DATA_BUFS);
        ++i;
    }
    if (r) {
        data_buf->write((char *)buffer + i * WINDOW_DATA_BUFS, r);
    }
}

#endif // CAPIO_POSIX_UTILS_SHM_HPP

#ifndef CAPIO_POSIX_UTILS_DATA_HPP
#define CAPIO_POSIX_UTILS_DATA_HPP

#include "types.hpp"

CPThreadDataBufs_t *threads_data_bufs;

/**
 * Initialize data buffers
 * @return
 */
inline void init_data_plane() { threads_data_bufs = new CPThreadDataBufs_t; }

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_data_listener(long tid) {
    auto *write_queue = new SPSCQueue<char>(
        "capio_write_data_buffer_tid_" + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
        CAPIO_DATA_BUFFER_ELEMENT_SIZE, CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES,
        get_capio_workflow_name());
    auto *read_queue = new SPSCQueue<char>("capio_read_data_buffer_tid_" + std::to_string(tid),
                                           CAPIO_DATA_BUFFER_LENGTH, CAPIO_DATA_BUFFER_ELEMENT_SIZE,
                                           CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES,
                                           get_capio_workflow_name());
    threads_data_bufs->insert({static_cast<int>(tid), {write_queue, read_queue}});
}

/**
 * Receives @count bytes for thread @tid and writes them into @buf
 * @param tid
 * @param buffer
 * @param count
 * @return
 */
inline void read_data(long tid, const void *buffer, off64_t count) {
    START_LOG(tid, "call(buffer=0x%08x, count=%ld)", buffer, count);
    auto data_buf  = threads_data_bufs->at(tid).second;
    size_t n_reads = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t r       = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t i       = 0;
    while (i < n_reads) {
        data_buf->read((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
        ++i;
    }
    if (r) {
        data_buf->read((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
    }
}

/**
 * Reads @count bytes from @buf and sends them for thread @tid
 * @param tid
 * @param buffer
 * @param count
 * @return
 */
inline void write_data(long tid, const void *buffer, off64_t count) {
    START_LOG(tid, "call(buffer=0x%08x, count=%ld)", buffer, count);
    auto data_buf   = threads_data_bufs->at(tid).first;
    size_t n_writes = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t r        = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;

    size_t i = 0;
    while (i < n_writes) {
        data_buf->write((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
        ++i;
    }
    if (r) {
        data_buf->write((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
    }
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

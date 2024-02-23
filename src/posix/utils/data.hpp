#ifndef CAPIO_POSIX_UTILS_DATA_HPP
#define CAPIO_POSIX_UTILS_DATA_HPP

#include "cache.hpp"

CPThreadDataCache_t *threads_data_cache;

/**
 * Initialize data buffers
 * @return
 */
inline void init_data_plane() { threads_data_cache = new CPThreadDataCache_t; }

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_data_listener(long tid) {
    auto *write_queue = new SPSCQueue<char>(
        "capio_write_data_buffer_tid_" + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
        CAPIO_DATA_BUFFER_ELEMENT_SIZE, CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES);
    auto *write_cache = new WriteCache(write_queue, tid, get_caching_data_buf_size());
    auto *read_queue = new SPSCQueue<char>("capio_read_data_buffer_tid_" + std::to_string(tid),
                                           CAPIO_DATA_BUFFER_LENGTH, CAPIO_DATA_BUFFER_ELEMENT_SIZE,
                                           CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES);
    auto *read_cache = new ReadCache(read_queue, tid, get_caching_data_buf_size());
    threads_data_cache->insert({static_cast<int>(tid), {write_cache, read_cache}});
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
    auto reader_cache = threads_data_cache->at(tid).second;
    size_t n_reads = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t r       = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t i       = 0;
    while (i < n_reads) {
        reader_cache->read((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
        ++i;
    }
    if (r) {
        reader_cache->read((char *) buffer + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
    }
}

/**
 * Reads @count bytes from @buf and sends them for thread @tid
 * @param tid
 * @param buffer
 * @param count
 * @return
 */
inline void write_data(long tid, int fd, const void *buffer, off64_t count) {
    START_LOG(tid, "call(buffer=0x%08x, count=%ld)", buffer, count);

    threads_data_cache->at(tid).first->write(fd, buffer, count);
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

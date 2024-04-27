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
    auto *write_queue =
        new SPSCQueue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
                      CAPIO_DATA_BUFFER_ELEMENT_SIZE, get_capio_workflow_name());
    auto *read_queue =
        new SPSCQueue(SHM_SPSC_PREFIX_READ + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
                      CAPIO_DATA_BUFFER_ELEMENT_SIZE, get_capio_workflow_name());
    threads_data_bufs->insert({static_cast<int>(tid), {write_queue, read_queue}});
}

/**
 * Receives @count bytes for thread @tid and writes them into @buf
 * @param tid
 * @param buffer
 * @param count
 * @return
 */
inline void read_data(long tid, void *buffer, off64_t count) {
    START_LOG(tid, "call(buffer=0x%08x, count=%ld)", buffer, count);

    threads_data_bufs->at(tid).second->read(reinterpret_cast<char *>(buffer), count);
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

    threads_data_bufs->at(tid).first->write(reinterpret_cast<const char *>(buffer), count);
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

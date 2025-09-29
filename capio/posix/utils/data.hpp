#ifndef CAPIO_POSIX_UTILS_DATA_HPP
#define CAPIO_POSIX_UTILS_DATA_HPP

#include "cache.hpp"

CPThreadDataCache_t *threads_data_cache;

/**
 * Get read cache for thread @param tid
 * @param tid
 * @return the thread read cache
 */
inline ReadCache &get_read_cache(long tid) { return *threads_data_cache->at(tid).second; }

/**
 * Get write cache for thread @param tid
 * @param tid
 * @return the thread write cache
 */
inline WriteCache &get_write_cache(long tid) { return *threads_data_cache->at(tid).first; }

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
    threads_data_cache->insert(
        {static_cast<int>(tid),
         {new WriteCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name()),
          new ReadCache(tid, get_cache_lines(), get_cache_line_size(),
                        get_capio_workflow_name())}});
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

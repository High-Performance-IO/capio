#ifndef CAPIO_POSIX_UTILS_DATA_HPP
#define CAPIO_POSIX_UTILS_DATA_HPP

#include "cache.hpp"

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void initialize_data_queues(const long tid) {
    write_cache =
        new WriteCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
    read_cache =
        new ReadCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

#ifndef CAPIO_SERVER_UTILS_ENV_HPP
#define CAPIO_SERVER_UTILS_ENV_HPP

#include <cstdlib>

#include "capio/constants.hpp"

off64_t get_file_initial_size() {
    static off64_t file_initial_size = 0;
    if (file_initial_size == 0) {
        char *val;
        val = std::getenv("CAPIO_FILE_INIT_SIZE");
        if (val != nullptr) {
            file_initial_size = std::strtol(val, nullptr, 10);
        } else {
            file_initial_size = DEFAULT_FILE_INITIAL_SIZE;
        }
#ifdef CAPIOLOG
        logfile << "file_initial_size: " << file_initial_size << std::endl;
#endif
    }
    return file_initial_size;
}

off64_t get_prefetch_data_size() {
    static off64_t prefetch_data_size = -1;
    if (prefetch_data_size == -1) {
        char *val;
        val = std::getenv("CAPIO_PREFETCH_DATA_SIZE");
        if (val != nullptr) {
            prefetch_data_size = std::strtol(val, nullptr, 10);
        } else {
            prefetch_data_size = 0;
        }
#ifdef CAPIOLOG
        logfile << "prefetch_data_size: " << prefetch_data_size << std::endl;
#endif
    }
    return prefetch_data_size;
}

#endif // CAPIO_SERVER_UTILS_ENV_HPP

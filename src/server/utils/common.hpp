#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "types.hpp"




inline bool is_int(const std::string &s) {
    START_LOG(gettid(), "call(%s)", s.c_str());
    bool res = false;
    if (!s.empty()) {
        char *p;
        strtol(s.c_str(), &p, 10);
        res = *p == 0;
    }
    return res;
}

inline int find_batch_size(const std::string &glob, CSMetadataConfGlobs_t &metadata_conf_globs) {
    START_LOG(gettid(), "call(%s)", glob.c_str());
    bool found = false;
    int nfiles;
    std::size_t i = 0;

    while (!found && i < metadata_conf_globs.size()) {
        found = glob == std::get<0>(metadata_conf_globs[i]);
        ++i;
    }

    if (found) {
        nfiles = std::get<5>(metadata_conf_globs[i - 1]);
    } else {
        nfiles = -1;
    }

    return nfiles;
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP

#ifndef CAPIO_SERVER_UTILS_PRODUCER_HPP
#define CAPIO_SERVER_UTILS_PRODUCER_HPP

#include <string>

#include "metadata.hpp"

std::string get_producer_name(const std::string &path) {
    START_LOG(gettid(), "call( %s)", path.c_str());
    std::string producer_name;
    // we handle also prefixes
    auto it_metadata = metadata_conf.find(path);
    if (it_metadata == metadata_conf.end()) {
        long int pos = match_globs(path);
        if (pos != -1) {
            producer_name = std::get<3>(metadata_conf_globs[pos]);
        }
    } else {
        producer_name = std::get<2>(it_metadata->second);
    }

    return producer_name;
}

bool is_producer(int tid, const std::string &path) {
    START_LOG(tid, "call(%d, %s)", tid, path.c_str());
    bool res = false;
    auto it  = apps.find(tid);

    if (it != apps.end()) {
        std::string app_name  = apps[tid];
        std::string prod_name = get_producer_name(path);
        res                   = app_name == prod_name;
    }

    return res;
}

#endif // CAPIO_SERVER_UTILS_PRODUCER_HPP

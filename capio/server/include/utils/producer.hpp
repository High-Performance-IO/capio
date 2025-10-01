#ifndef CAPIO_SERVER_UTILS_PRODUCER_HPP
#define CAPIO_SERVER_UTILS_PRODUCER_HPP

#include <string>

#include "utils/metadata.hpp"

std::string get_producer_name(const std::filesystem::path &path) {
    START_LOG(gettid(), "call( %s)", path.c_str());

    // This version of CAPIO supports a single producer, however, the CAPIO-CL engine supports
    // multiple producers per each file. For now, we hence return the first entry of the producers.
    return capio_cl_engine->getProducers(path).at(0);
}

bool is_producer(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%d, %s)", tid, path.c_str());
    bool res = false;

    if (apps.find(tid) != apps.end()) {
        std::string app_name  = apps[tid];
        std::string prod_name = get_producer_name(path);
        res                   = app_name == prod_name;
    }

    return res;
}

#endif // CAPIO_SERVER_UTILS_PRODUCER_HPP

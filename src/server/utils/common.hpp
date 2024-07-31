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

#endif // CAPIO_SERVER_UTILS_COMMON_HPP

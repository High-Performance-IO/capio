#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <string>

#include "capio/constants.hpp"
#include "types.hpp"

bool first_is_subpath_of_second(const std::filesystem::path &path,
                                const std::filesystem::path &base) {
    const auto mismatch_pair = std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatch_pair.second == base.end();
}

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

/**
 * check if @param to_match is correct against @param glob
 * @param glob path with wildcard
 * @param to_match string to check
 * @return
 */
inline bool match_globs(std::string glob, std::string to_match) {
    bool matches = true;

    if (glob.empty()) {
        return false;
    }

    auto glob_size = glob.size(), to_match_size = to_match.size();
    int i = 0, j = 0;

    while (i < glob_size && j < to_match_size && matches) {
        if (glob[i] == '*') {
            if (glob.back() == '*') {
                return true;
            }
            std::string glob_end = glob.substr(glob.find('*') + 1, glob.size());
            return to_match.compare(to_match_size - glob_end.size(), glob_end.size(), glob_end);
        }

        if (glob[i] != '?') {
            matches = glob[i] == to_match[j];
        }

        i++;
        j++;
    }

    return matches;
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP

#ifndef CAPIO_COMMON_PATH_HPP
#define CAPIO_COMMON_PATH_HPP

#include <string>

/// @brief Whether @p path is @p name itself, or is inside the directory @p name
inline bool is_at_or_below(const std::string &path, const std::string &name) {
    return path.compare(0, name.size(), name) == 0 &&
           (path.size() == name.size() || path[name.size()] == '/');
}

#endif // CAPIO_COMMON_PATH_HPP

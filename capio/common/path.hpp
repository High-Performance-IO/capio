#ifndef CAPIO_COMMON_PATH_HPP
#define CAPIO_COMMON_PATH_HPP

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

/// @brief Whether @p path is @p name itself, or is inside the directory @p name
inline bool is_at_or_below(const std::string &path, const std::string &name) {
    return path.compare(0, name.size(), name) == 0 &&
           (path.size() == name.size() || path[name.size()] == '/');
}

/// @brief Paths under these prefixes are never handled by CAPIO, and cannot be CAPIO_DIR
constexpr std::array CAPIO_FORBIDDEN_PREFIXES = {std::string_view{"/proc/"},
                                                 std::string_view{"/sys/"}};

/// @brief Suffixes of the token files that CAPIO-CL's file system monitor creates next to a file
/// (.<name>.commit and .<name>.home_node). They are not files of the workflow.
constexpr std::array CAPIO_CL_TOKEN_SUFFIXES = {std::string_view{".commit"},
                                                std::string_view{".home_node"}};

/**
 * @brief Whether CAPIO must ignore @p path: it starts with a forbidden prefix, or it is one of
 * CAPIO-CL's token files. @p path can be a full path or just a file name (e.g. a directory entry).
 */
inline bool is_forbidden_path(const std::string_view path) {
    const auto starts_with = [path](const std::string_view prefix) {
        return path.compare(0, prefix.size(), prefix) == 0;
    };
    const auto ends_with =
        [path](const std::string_view suffix) { // string_view::ends_with is C++20
            return path.size() >= suffix.size() &&
                   path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
    return std::any_of(CAPIO_FORBIDDEN_PREFIXES.begin(), CAPIO_FORBIDDEN_PREFIXES.end(),
                       starts_with) ||
           std::any_of(CAPIO_CL_TOKEN_SUFFIXES.begin(), CAPIO_CL_TOKEN_SUFFIXES.end(), ends_with);
}

#endif // CAPIO_COMMON_PATH_HPP

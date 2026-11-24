#ifndef CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP
#define CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

#include <semaphore.h>

#include <atomic>
#include <climits>
#include <string>
#include <string_view>
#include <vector>

#include "constants.hpp"

struct linux_dirent64 {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;    /* File type */
    char d_name[NAME_MAX];   /* Filename (null-terminated) */
};

/// @brief Whether @p name is a token that CAPIO-CL's file system monitor leaves next to a file
inline bool is_capio_cl_token(const std::string_view name) {
    const auto ends_with = [name](const std::string_view suffix) {
        return name.size() > suffix.size() + 1 &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return !name.empty() && name[0] == '.' && (ends_with(".commit") || ends_with(".home_node"));
}

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

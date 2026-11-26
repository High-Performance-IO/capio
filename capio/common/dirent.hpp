#ifndef CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP
#define CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

#include <semaphore.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstring>
#include <string>
#include <vector>

#include "constants.hpp"

struct linux_dirent64 {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;    /* File type */
    char d_name[NAME_MAX];   /* Filename (null-terminated) */
};

/// @brief FS variant: the record that the server sends in place of the entries of a directory, to
/// let the kernel list it. Every byte is 0xFF, which no entry is.
inline constexpr auto KERNEL_LISTS_MARKER = [] {
    std::array<unsigned char, sizeof(linux_dirent64)> marker{};
    for (auto &byte : marker) {
        byte = 0xFF;
    }
    return marker;
}();

inline bool is_kernel_lists_marker(const linux_dirent64 &record) {
    return memcmp(&record, KERNEL_LISTS_MARKER.data(), KERNEL_LISTS_MARKER.size()) == 0;
}

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

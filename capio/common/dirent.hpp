#ifndef CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP
#define CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

#include <semaphore.h>

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
inline linux_dirent64 kernel_lists_marker() {
    linux_dirent64 marker;
    memset(&marker, 0xFF, sizeof(marker));
    return marker;
}

inline bool is_kernel_lists_marker(const linux_dirent64 &record) {
    const linux_dirent64 marker = kernel_lists_marker();
    return memcmp(&record, &marker, sizeof(marker)) == 0;
}

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

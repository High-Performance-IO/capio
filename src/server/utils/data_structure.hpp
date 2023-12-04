#ifndef CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP
#define CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

#include <semaphore.h>

#include <atomic>
#include <climits>
#include <string>
#include <vector>

#include "capio/constants.hpp"
#include "capio_file.hpp"

struct linux_dirent {
    ino64_t        d_ino;    /* 64-bit inode number */
    off64_t        d_off;    /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char  d_type;   /* File type */
    char           d_name[]; /* Filename (null-terminated) */
};

struct linux_dirent64 {
    ino64_t        d_ino;    /* 64-bit inode number */
    off64_t        d_off;    /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char  d_type;   /* File type */
    char           d_name[]; /* Filename (null-terminated) */
};

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

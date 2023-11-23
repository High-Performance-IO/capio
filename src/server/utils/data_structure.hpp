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
    unsigned long d_ino;       /* Inode number */
    unsigned long d_off;       /* Offset to next linux_dirent */
    unsigned short d_reclen;   /* Length of this linux_dirent */
    char d_name[DNAME_LENGTH]; /* Filename (null-terminated) */
    /* length is actually (d_reclen - 2 - offsetof(struct linux_dirent, d_name)) */
    char pad;    // Zero padding byte
    char d_type; // File type (only since Linux
                 // 2.6.4); offset is (d_reclen - 1)
};

struct linux_dirent64 {
    ino64_t d_ino;             /* 64-bit inode number */
    off64_t d_off;             /* 64-bit offset to next structure */
    unsigned short d_reclen;   /* Size of this dirent */
    unsigned char d_type;      /* File type */
    char d_name[DNAME_LENGTH]; /* Filename (null-terminated) */
};

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

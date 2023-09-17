#ifndef CAPIO_COMMON_CONSTANTS_HPP
#define CAPIO_COMMON_CONSTANTS_HPP

#include <sys/types.h>

size_t dir_initial_size = 1024L * 1024 * 1024;

constexpr int DNAME_LENGTH = 128;

// default initial size for each file (can be overwritten by the user)
off64_t DEFAULT_FILE_INITIAL_SIZE = 1024L * 1024 * 1024 * 4;

// maximum size of shm
constexpr long MAX_SHM_SIZE = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
constexpr long MAX_SHM_SIZE_FILE = 1024L * 1024 * 1024 * 16;

constexpr int N_ELEMS_DATA_BUFS = 10;

constexpr int THEORETICAL_SIZE_DIRENT64 = sizeof(ino64_t) + sizeof(off64_t) + sizeof(unsigned short) + sizeof(unsigned char) + sizeof(char) * (DNAME_LENGTH + 1);

constexpr int THEORETICAL_SIZE_DIRENT = sizeof(unsigned long) + sizeof(off_t) + sizeof(unsigned short) + sizeof(char) * (DNAME_LENGTH + 2);

constexpr int WINDOW_DATA_BUFS = 256 * 1024;

#endif // CAPIO_COMMON_CONSTANTS_HPP

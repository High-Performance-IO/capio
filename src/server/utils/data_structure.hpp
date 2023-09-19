#ifndef CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP
#define CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP


#include <atomic>
#include <climits>
#include <string>
#include <vector>

#include <semaphore.h>

#include "capio/constants.hpp"

#include "capio_file.hpp"

struct linux_dirent {
    unsigned long  d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    char           d_name[DNAME_LENGTH + 2];
};

struct linux_dirent64 {
    ino64_t  	   d_ino;
    off64_t        d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char           d_name[DNAME_LENGTH + 1];
};

#endif // CAPIO_SERVER_UTILS_DATA_STRUCTURE_HPP

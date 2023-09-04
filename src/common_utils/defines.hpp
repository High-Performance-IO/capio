#ifndef CAPIO_DEFINES_HPP
#define CAPIO_DEFINES_HPP

#define N_ELEMS_DATA_BUFS 10

#define WINDOW_DATA_BUFS 262144 //256KB

#define DNAME_LENGTH 128

constexpr static int theoretical_size_dirent64 = sizeof(ino64_t) + sizeof(off64_t) + sizeof(unsigned short) + sizeof(unsigned char) + sizeof(char) * (DNAME_LENGTH + 1);

constexpr static int theoretical_size_dirent = sizeof(unsigned long) + sizeof(off_t) + sizeof(unsigned short) + sizeof(char) * (DNAME_LENGTH + 2);


const long int max_shm_size = 1024L * 1024 * 1024 * 16;

// maximum size of shm for each file
const long int max_shm_size_file = 1024L * 1024 * 1024 * 8;

// initial size for each file (can be overwritten by the user)
size_t file_initial_size = 1024L * 1024 * 1024 * 4;

const size_t dir_initial_size = 1024L * 1024 * 1024;

#endif //CAPIO_DEFINES_HPP

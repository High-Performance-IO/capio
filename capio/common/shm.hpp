#ifndef CAPIO_COMMON_SHM_HPP
#define CAPIO_COMMON_SHM_HPP

#include <string>
#include <utility>

#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/logger.hpp"

#ifdef __CAPIO_POSIX

#define SHM_DESTROY_CHECK(source_name)                                                             \
    if (shm_unlink(source_name) == -1) {                                                           \
        ERR_EXIT_EXCEPT_CHOICE(false, "Unable to destroy shared mem: %s", source_name);            \
    };

#define SHM_CREATE_CHECK(condition, source)                                                        \
    if (condition) {                                                                               \
        ERR_EXIT("Unable to open shm: %s", source);                                                \
    };

#else

#define SHM_DESTROY_CHECK(source_name)                                                             \
    if (shm_unlink(source_name) == -1) {                                                           \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "Unable to destroy shared mem: '"       \
                  << source_name << "' (" << strerror(errno) << ")" << std::endl;                  \
    };

#define SHM_CREATE_CHECK(condition, source)                                                        \
    if (condition) {                                                                               \
        LOG("error while creating %s", source);                                                    \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Unable to create shm: " << source        \
                  << std::endl;                                                                    \
        ERR_EXIT("Unable to open shm: %s", source);                                                \
    };

#endif

inline void *create_shm(const std::string &shm_name, const long int size) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s, size=%ld)", shm_name.c_str(), size);

    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,
                      S_IRUSR | S_IWUSR); // to be closed
    SHM_CREATE_CHECK(fd == -1, shm_name.c_str());

    if (ftruncate(fd, size) == -1) {
        ERR_EXIT("ftruncate create_shm %s", shm_name.c_str());
    }
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap create_shm %s: %s", shm_name.c_str(), strerror(errno));
    }
    if (close(fd) == -1) {

        ERR_EXIT("close");
    }
    return p;
}

inline void *get_shm(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0); // to be closed
    struct stat sb {};
    if (fd == -1) {
        ERR_EXIT("get_shm shm_open %s", shm_name.c_str());
    }
    /* Open existing object */
    /* Use shared memory object size as length argument for mmap()
    and as number of bytes to write() */
    if (fstat(fd, &sb) == -1) {
        ERR_EXIT("fstat %s", shm_name.c_str());
    }
    void *p = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap get_shm %s: %s", shm_name.c_str(), strerror(errno));
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

inline void *get_shm_if_exist(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0); // to be closed
    struct stat sb {};
    if (fd == -1) {
        if (errno == ENOENT) {
            return nullptr;
        }
        ERR_EXIT("get_shm shm_open %s", shm_name.c_str());
    }
    /* Open existing object */
    /* Use shared memory object size as length argument for mmap()
    and as number of bytes to write() */
    if (fstat(fd, &sb) == -1) {
        ERR_EXIT("fstat %s", shm_name.c_str());
    }
    void *p = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap get_shm %s : %s", shm_name.c_str(), strerror(errno));
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

#endif // CAPIO_COMMON_SHM_HPP

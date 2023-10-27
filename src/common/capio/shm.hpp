#ifndef CAPIO_COMMON_SHM_HPP
#define CAPIO_COMMON_SHM_HPP

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

void *create_shm(const std::string &shm_name, const long int size) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s, size=%ld)", shm_name.c_str(), size);

    void *p;
    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,
                      S_IRUSR | S_IWUSR); // to be closed
    if (fd == -1) {
        ERR_EXIT("create_shm shm_open %s", shm_name.c_str());
    }
    if (ftruncate(fd, size) == -1) {
        ERR_EXIT("ftruncate create_shm %s", shm_name.c_str());
    }
    p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap create_shm %s", shm_name.c_str());
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

void *get_shm(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    void *p;
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
    p = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap get_shm %s", shm_name.c_str());
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

void *get_shm_if_exist(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    void *p;
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
    p = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ERR_EXIT("mmap get_shm %s", shm_name.c_str());
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

#endif // CAPIO_COMMON_SHM_HPP

#ifndef CAPIO_COMMON_SHM_HPP
#define CAPIO_COMMON_SHM_HPP

#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "capio/logger.hpp"

#ifdef __CAPIO_POSIX

#define SHM_DESTROY_CHECK(source_name)                                                             \
    if (shm_unlink(source_name) == -1) {                                                           \
        if (errno != ENOENT) {                                                                     \
            ERR_EXIT("Unable to destroy shared mem:  %s. Error is %s", source_name,                \
                     strerror(errno));                                                             \
        } else {                                                                                   \
            LOG("Warn: shm segment %s is not present on FS", source_name);                         \
        }                                                                                          \
    }

#define SHM_CREATE_CHECK(condition, source)                                                        \
    if (condition) {                                                                               \
        ERR_EXIT("Unable to open shm: %s: %s", source, strerror(errno));                           \
    };

#else

#define SHM_DESTROY_CHECK(source_name)                                                             \
    if (shm_unlink(source_name) == -1) {                                                           \
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, "Unable to destroy shared mem: '" +       \
                                                             std::string(source_name) + "' (" +    \
                                                             strerror(errno) + ")");               \
    };

#define SHM_CREATE_CHECK(condition, source)                                                        \
    if (condition) {                                                                               \
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, "Unable to create shared mem: '" +        \
                                                             std::string(source) + "' (" +         \
                                                             strerror(errno) + ")");               \
        ERR_EXIT("Unable to open shm %s: %s", source, strerror(errno));                            \
    };

#endif

class CapioShmCanary {
    int _shm_id;
    std::string _canary_name;

  public:
    explicit CapioShmCanary(std::string capio_workflow_name = CAPIO_DEFAULT_WORKFLOW_NAME)
        : _canary_name(get_capio_workflow_name()) {
        START_LOG(capio_syscall(SYS_gettid), "call(capio_workflow_name: %s)", _canary_name.data());
        if (_canary_name.empty()) {
            LOG("Empty _canary_name");
            _canary_name = get_capio_workflow_name();
        }
        _shm_id = shm_open(_canary_name.data(), O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (_shm_id == -1) {
            LOG(CAPIO_SHM_CANARY_ERROR, _canary_name.data());
#ifndef __CAPIO_POSIX
            auto message = new char[strlen(CAPIO_SHM_CANARY_ERROR)];
            sprintf(message, CAPIO_SHM_CANARY_ERROR, _canary_name.data());
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << message << std::endl;
            delete[] message;
#endif
            ERR_EXIT("ERR: shm canary flag already exists");
        }
    };

    ~CapioShmCanary() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
#ifndef __CAPIO_POSIX
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Removing shared memory canary flag");
#endif

#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = true;
#endif
        close(_shm_id);
        SHM_DESTROY_CHECK(_canary_name.c_str());
#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = false;
#endif

#ifndef __CAPIO_POSIX
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Shutdown completed");
#endif
    }
};

CapioShmCanary *shm_canary;

void *create_shm(const std::string &shm_name, const long int size) {
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
        ERR_EXIT("mmap create_shm %s", shm_name.c_str());
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

auto get_shm_size(int shm_fd, const char *shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld)", shm_fd);
    struct stat sb = {0};
    /* Open existing object */
    /* Use shared memory object size as length argument for mmap()
    and as number of bytes to write() */
    if (fstat(shm_fd, &sb) == -1) {
        ERR_EXIT("fstat %s", shm_name);
    }

    if (sb.st_size <= 0) {
        LOG("WARN: size of stat is %ld. Retry once.", sb.st_size);
        if (fstat(shm_fd, &sb) == -1) {
            ERR_EXIT("fstat %s", shm_name);
        }
        if (sb.st_size <= 0) {
            LOG("WARN: retry no. 2 gave a size of %ld", sb.st_size);
            ERR_EXIT("FATAL: unable to obtain size of shm object %s after two tries...", shm_name);
        }
    }

    LOG("Size of shm object %s : %ld", shm_name, sb.st_size);
    return sb.st_size;
}

void *get_shm(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0); // to be closed
    if (fd == -1) {
        ERR_EXIT("get_shm shm_open %s", shm_name.c_str());
    }

    auto size = get_shm_size(fd, shm_name.c_str());

    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        LOG("ERROR MMAP arg dump:");
        LOG("mmap-size:  %ld", size);
        LOG("mmap-prot:  %ld", PROT_READ | PROT_WRITE);
        LOG("mmap-flags: %ld", MAP_SHARED);
        LOG("mmap-fd:    %ld", fd);
        ERR_EXIT("ERROR: mmap failed at get_shm(%s): %s", shm_name.c_str(), strerror(errno));
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

void *get_shm_if_exist(const std::string &shm_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s)", shm_name.c_str());

    // if we are not creating a new object, mode is equals to 0
    int fd = shm_open(shm_name.c_str(), O_RDWR, 0); // to be closed

    if (fd == -1) {
        if (errno == ENOENT) {
            return nullptr;
        }
        ERR_EXIT("ERROR: unable to open shared memory %s: %s", shm_name.c_str(), strerror(errno));
    }

    auto size = get_shm_size(fd, shm_name.c_str());

    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        LOG("ERROR MMAP arg dump:");
        LOG("mmap-size:  %ld", size);
        LOG("mmap-prot:  %ld", PROT_READ | PROT_WRITE);
        LOG("mmap-flags: %ld", MAP_SHARED);
        LOG("mmap-fd:    %ld", fd);
        ERR_EXIT("ERROR: mmap failed at get_shm_if_exist(%s): %s", shm_name.c_str(),
                 strerror(errno));
    }
    if (close(fd) == -1) {
        ERR_EXIT("close");
    }
    return p;
}

#endif // CAPIO_COMMON_SHM_HPP

#ifndef CAPIO_SEMS_HPP
#define CAPIO_SEMS_HPP

#include <semaphore.h>

#include <utility>

#include "capio/logger.hpp"

class NoLock {
  private:
    const std::string _name;

  public:
    NoLock(std::string name, unsigned int init_value) : _name(std::move(name)) {
        START_LOG(capio_syscall(SYS_gettid), "call(name=%s, initial_value=%d)", _name.c_str(),
                  init_value);
    }

    NoLock(const NoLock &)            = delete;
    NoLock &operator=(const NoLock &) = delete;
    ~NoLock()                         = default;

    inline void lock(){};
    inline void unlock(){};
};

class Semaphore {
  private:
    const std::string _name;
    sem_t *_sem;

  public:
    Semaphore(std::string name, unsigned int init_value) : _name(std::move(name)) {
        START_LOG(capio_syscall(SYS_gettid), "call(name=%s, initial_value=%d)", _name.c_str(),
                  init_value);

        _sem = sem_open(_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, init_value);
        if (_sem == SEM_FAILED) {
            ERR_EXIT("sem_open %s failed", _name.c_str());
        }
    }

    Semaphore(const Semaphore &)            = delete;
    Semaphore &operator=(const Semaphore &) = delete;

    ~Semaphore() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_destroy(_sem) != 0) {
            ERR_EXIT("destruction of semaphore %s failed", _name.c_str());
        }
    }

    void lock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_wait(_sem) == -1) {
            ERR_EXIT("unable to acquire %s", _name.c_str());
        }
    }

    void unlock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_post(_sem) == -1) {
            ERR_EXIT("unable to release %s", _name.c_str());
        }
    }
};

#ifdef __CAPIO_POSIX

#define SEM_WAIT_CHECK(sem, sem_name)                                                              \
    if (sem_wait(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to wait on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    };

#define SEM_POST_CHECK(sem, sem_name)                                                              \
    if (sem_post(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to post on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    };

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    if (sem_destroy(sem) != 0) {                                                                   \
        action;                                                                                    \
        ERR_EXIT("destruction of semaphore %s failed", sem_name);                                  \
    };

#define SEM_NAMED_DESTROY_CHECK(sem)                                                               \
    if (sem_unlink(sem) != 0) {                                                                    \
        ERR_EXIT("destruction of semaphore %s failed", sem);                                       \
    };

#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        LOG(CAPIO_SHM_OPEN_ERROR);                                                                 \
        LOG("error creating opening %s", _shm_name);                                               \
    };

#else

#define SEM_WAIT_CHECK(sem, sem_name)                                                              \
    if (sem_wait(sem) == -1) {                                                                     \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to wait on semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to wait on semaphore:  %s", sem_name);                                    \
    };

#define SEM_POST_CHECK(sem, sem_name)                                                              \
    if (sem_post(sem) == -1) {                                                                     \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to post on semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to post on semaphore:  %s", sem_name);                                    \
    };

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    if (sem_destroy(sem) != 0) {                                                                   \
        action;                                                                                    \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to destroy semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to destroy semaphore:  ", sem_name);                                      \
    };

#define SEM_NAMED_DESTROY_CHECK(sem_name)                                                          \
    if (sem_unlink(sem_name) == -1) {                                                              \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to destroy semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to destroy semaphore:  ", sem_name);                                      \
    }

#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        LOG(CAPIO_SHM_OPEN_ERROR);                                                                 \
        LOG("error while opening %s", source);                                                     \
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "Unable to open shm: " << source         \
                  << std::endl;                                                                    \
        ERR_EXIT("Unable to open shm: %s", source);                                                \
    };

#endif

#endif // CAPIO_SEMS_HPP
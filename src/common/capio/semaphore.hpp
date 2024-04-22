#ifndef CAPIO_SEMS_HPP
#define CAPIO_SEMS_HPP

#include <semaphore.h>

#include <utility>

#include "capio/logger.hpp"

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
    }

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    if (sem_destroy(sem) != 0) {                                                                   \
        action;                                                                                    \
        ERR_EXIT("destruction of semaphore %s failed", sem_name);                                  \
    };

class NoLock {
  public:
    NoLock(const std::string &name, unsigned int init_value) {
        START_LOG(capio_syscall(SYS_gettid), "call(name=%s, initial_value=%d)", name.c_str(),
                  init_value);
    }

    NoLock(const NoLock &)            = delete;
    NoLock &operator=(const NoLock &) = delete;
    ~NoLock()                         = default;

    inline void lock() {};
    
    inline void unlock() {};
};

class NamedSemaphore {
  private:
    const std::string _name;
    sem_t *_sem;

  public:
    NamedSemaphore(std::string name, unsigned int init_value) : _name(std::move(name)) {
        START_LOG(capio_syscall(SYS_gettid), "[  NamedSemaphore  ] call(name=%s, init_value=%d)",
                  _name.c_str(), init_value);

        _sem = sem_open(_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, init_value);
        if (_sem == SEM_FAILED) {
            ERR_EXIT("[  NamedSemaphore  ] sem_open %s failed", _name.c_str());
        }
    }

    NamedSemaphore(const NamedSemaphore &)            = delete;
    NamedSemaphore &operator=(const NamedSemaphore &) = delete;
    ~NamedSemaphore() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_destroy(_sem) != 0) {
            ERR_EXIT("[  NamedSemaphore  ] destruction of semaphore %s failed", _name.c_str());
        }
        LOG("[  NamedSemaphore  ] Destroyed shared semaphore %s", _name.c_str());
        if (sem_unlink(_name.c_str()) != 0) {
            ERR_EXIT("[  NamedSemaphore  ] destruction of semaphore %s failed", _name.c_str());
        }
        LOG("[  NamedSemaphore  ] Unlinked shared semaphore %s", _name.c_str());
    }

    inline void lock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        LOG("[  NamedSemaphore  ] Acquiring lock on %s", _name.c_str());

        if (sem_wait(_sem) == -1) {
            ERR_EXIT("[  NamedSemaphore  ] unable to acquire %s", _name.c_str());
        }

        LOG("[  NamedSemaphore  ] Acquired lock on %s", _name.c_str());
    }

    inline void unlock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        LOG("[  NamedSemaphore  ] Releasing lock on %s", _name.c_str());

        if (sem_post(_sem) == -1) {
            ERR_EXIT("[  NamedSemaphore  ] unable to release %s", _name.c_str());
        }

        LOG("[  NamedSemaphore  ] Released lock on %s", _name.c_str());
    }
};

class Semaphore {
  private:
    sem_t _sem{};

  public:
    explicit Semaphore(unsigned int init_value) {
        START_LOG(capio_syscall(SYS_gettid), "[  Semaphore  ] call(init_value=%d)", init_value);

        if (sem_init(&_sem, 0, init_value) != 0) {
            ERR_EXIT("[  Semaphore  ] initialization of unnamed semaphore failed");
        }
    }

    Semaphore(const Semaphore &)            = delete;
    Semaphore &operator=(const Semaphore &) = delete;
    ~Semaphore() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_destroy(&_sem) != 0) {
            ERR_EXIT("[  Semaphore  ] destruction of unnamed semaphore failed");
        }
    }

    inline void lock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_wait(&_sem) == -1) {
            ERR_EXIT("[  Semaphore  ] unable to acquire unnamed semaphore");
        }
    }

    inline void unlock() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        if (sem_post(&_sem) == -1) {
            ERR_EXIT("[  Semaphore  ] unable to release unnamed semaphore");
        }
    }
};

#endif // CAPIO_SEMS_HPP
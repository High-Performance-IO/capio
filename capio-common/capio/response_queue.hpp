#ifndef CAPIO_RESPONSE_QUEUE_HPP
#define CAPIO_RESPONSE_QUEUE_HPP

#include <iostream>
#include <mutex>

#include <semaphore.h>

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"
#include "capio/shm.hpp"

/**
 * @brief Efficient implementation of class Queue for offset responses
 *
 * @tparam T Type of data that is being transported
 */
class ResponseQueue {
    void *_shm;
    const std::string _shm_name;
    bool require_cleanup;
    NamedSemaphore _shared_mutex;

  public:
    explicit ResponseQueue(const std::string &shm_name, bool cleanup = true)
        : _shm_name(get_capio_workflow_name() + "_" + shm_name), require_cleanup(cleanup),
          _shared_mutex(get_capio_workflow_name() + SHM_SEM_ELEMS + shm_name, 0, cleanup) {
        START_LOG(capio_syscall(SYS_gettid), "call(shm_name=%s, workflow_name=%s, cleanup=%s)",
                  shm_name.data(), get_capio_workflow_name().data(), cleanup ? "yes" : "no");
#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = true;
#endif

        _shm = get_shm_if_exist(_shm_name);
        if (_shm == nullptr) {
            _shm = create_shm(_shm_name, sizeof(capio_off64_t));
        }
#ifdef __CAPIO_POSIX
        syscall_no_intercept_flag = false;
#endif
    }

    ResponseQueue(const ResponseQueue &)            = delete;
    ResponseQueue &operator=(const ResponseQueue &) = delete;

    ~ResponseQueue() {
        START_LOG(capio_syscall(SYS_gettid), "call(_shm_name=%s)", _shm_name.c_str());
        if (require_cleanup) {
            LOG("Performing cleanup of allocated resources");
#ifdef __CAPIO_POSIX
            syscall_no_intercept_flag = true;
#endif
            SHM_DESTROY_CHECK(_shm_name.c_str());
#ifdef __CAPIO_POSIX
            syscall_no_intercept_flag = false;
#endif
        }
    }

    auto read() {
        _shared_mutex.lock();
        return *static_cast<capio_off64_t *>(_shm);
    }

    void write(const capio_off64_t data) {
        *static_cast<capio_off64_t *>(_shm) = data;
        _shared_mutex.unlock();
    }
};
#endif // CAPIO_RESPONSE_QUEUE_HPP
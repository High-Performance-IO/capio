#ifndef CAPIO_COMMON_QUEUE_HPP
#define CAPIO_COMMON_QUEUE_HPP

#include <iostream>
#include <mutex>

#include <semaphore.h>

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"
#include "capio/shm.hpp"

template <class T, class Mutex> class Queue {
  private:
    void *_shm;
    const long int _max_num_elems, _elem_size; // elements size in bytes
    long int _buff_size;                       // buffer size in bytes
    long int *_first_elem = nullptr, *_last_elem = nullptr;
    const std::string _shm_name, _first_elem_name, _last_elem_name;
    Mutex _mutex;
    NamedSemaphore _sem_num_elems, _sem_num_empty;

  public:
    Queue(const std::string &shm_name, const long int max_num_elems, const long int elem_size,
          const std::string &workflow_name = get_capio_workflow_name())
        : _max_num_elems(max_num_elems), _elem_size(elem_size),
          _buff_size(_max_num_elems * _elem_size), _shm_name(workflow_name + "_" + shm_name),
          _first_elem_name(workflow_name + SHM_FIRST_ELEM + shm_name),
          _last_elem_name(workflow_name + SHM_LAST_ELEM + shm_name),
          _mutex(workflow_name + SHM_MUTEX_PREFIX + shm_name, 1),
          _sem_num_elems(workflow_name + SHM_SEM_ELEMS + shm_name, 0),
          _sem_num_empty(workflow_name + SHM_SEM_EMPTY + shm_name, max_num_elems) {
        START_LOG(capio_syscall(SYS_gettid),
                  "call(shm_name=%s, _max_num_elems=%ld, elem_size=%ld, "
                  "workflow_name=%s)",
                  shm_name.data(), max_num_elems, elem_size, workflow_name.data());

        _first_elem = (long int *) create_shm(_first_elem_name, sizeof(long int));
        _last_elem  = (long int *) create_shm(_last_elem_name, sizeof(long int));
        _shm        = get_shm_if_exist(_shm_name);
        if (_shm == nullptr) {
            *_first_elem = 0;
            *_last_elem  = 0;
            _shm         = create_shm(_shm_name, _buff_size);
        }
    }

    Queue(const Queue &)            = delete;
    Queue &operator=(const Queue &) = delete;

    ~Queue() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        SHM_DESTROY_CHECK(_shm_name.c_str());
        SHM_DESTROY_CHECK(_first_elem_name.c_str());
        SHM_DESTROY_CHECK(_last_elem_name.c_str());
    }

    inline void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(data=0x%08x)", data);

        if (num_bytes > _elem_size) {
            ERR_EXIT("Queue %s write error: num_bytes > _elem_size", _shm_name.c_str());
        }

        _sem_num_empty.lock();
        std::lock_guard<Mutex> lg(_mutex);

        memcpy((T *) _shm + *_last_elem, data, num_bytes);
        *_last_elem = (*_last_elem + num_bytes) % _buff_size;
        LOG("Wrote '%s' on %s", data, this->_shm_name.c_str());

        _sem_num_elems.unlock();
    }

    inline void write(const T *data) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        write(data, _elem_size);
    }

    inline void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x)", buff_rcv);

        if (num_bytes > _elem_size) {
            ERR_EXIT("Queue %s read error: num_bytes > _elem_size", _shm_name.c_str());
        }

        _sem_num_elems.lock();
        std::lock_guard<Mutex> lg(_mutex);

        memcpy((T *) buff_rcv, ((T *) _shm) + *_first_elem, num_bytes);
        *_first_elem = (*_first_elem + num_bytes) % _buff_size;
        LOG("Received '%s' on %s", buff_rcv, this->_shm_name.c_str());

        _sem_num_empty.unlock();
    }

    inline void read(T *buf_rcv) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        read(buf_rcv, _elem_size);
    }
};

// Circular Buffer queue for requests
template <class T> using CircularBuffer = Queue<T, NamedSemaphore>;

// Single Producer Single Consumer queue
using SPSCQueue = Queue<char, NoLock>;

#endif // CAPIO_COMMON_QUEUE_HPP
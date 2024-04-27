#ifndef CAPIO_QUEUE_HPP
#define CAPIO_QUEUE_HPP

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

    inline void _read(T *buff_recv, int num_bytes) {
        _sem_num_elems.lock();

        std::lock_guard<Mutex> lg(_mutex);
        memcpy(reinterpret_cast<char *>(buff_recv), reinterpret_cast<char *>(_shm) + *_first_elem,
               num_bytes);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;

        _sem_num_empty.unlock();
    }

    inline void _write(const T *data, int num_bytes) {
        _sem_num_empty.lock();

        std::lock_guard<Mutex> lg(_mutex);
        memcpy(reinterpret_cast<char *>(_shm) + *_last_elem, reinterpret_cast<const char *>(data),
               num_bytes);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;

        _sem_num_elems.unlock();
    }

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
        START_LOG(capio_syscall(SYS_gettid),
                  "call(_shm_name=%s, _first_elem_name=%s, _last_elem_name=%s)", _shm_name.c_str(),
                  _first_elem_name.c_str(), _last_elem_name.c_str());
        SHM_DESTROY_CHECK(_shm_name.c_str());
        SHM_DESTROY_CHECK(_first_elem_name.c_str());
        SHM_DESTROY_CHECK(_last_elem_name.c_str());
    }

    inline auto get_name() { return this->_shm_name; }

    inline void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(data=0x%08x)", data);

        off64_t n_writes = num_bytes / _elem_size;
        size_t r         = num_bytes % _elem_size;

        for (int i = 0; i < n_writes; i++) {
            _write(data + i * _elem_size, _elem_size);
        }
        if (r) {
            _write(data + n_writes * _elem_size, r);
        }
    }

    inline void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x)", buff_rcv);

        off64_t n_reads = num_bytes / _elem_size;
        size_t r        = num_bytes % _elem_size;

        for (int i = 0; i < n_reads; i++) {
            _read(buff_rcv + i * _elem_size, _elem_size);
        }
        if (r) {
            _read(buff_rcv + n_reads * _elem_size, r);
        }
    }

    inline void read(T *buf_rcv) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x)", buf_rcv);
        this->read(buf_rcv, _elem_size);
    }

    inline void write(const T *data) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        this->write(data, _elem_size);
    }
};

// Circular Buffer queue for requests
template <class T> using CircularBuffer = Queue<T, NamedSemaphore>;

// Single Producer Single Consumer queue
using SPSCQueue = Queue<char, NoLock>;
#endif // CAPIO_QUEUE_HPP

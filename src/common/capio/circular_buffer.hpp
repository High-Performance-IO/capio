#ifndef CAPIO_COMMON_CIRCULAR_BUFFER_HPP
#define CAPIO_COMMON_CIRCULAR_BUFFER_HPP

#include <iostream>
#include <mutex>

#include <semaphore.h>

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"
#include "capio/shm.hpp"

/*
 * Multi-producer and multi-consumer circular buffer.
 * Each element of the circular buffer has the same size.
 */

template <class T, typename S> class CircularBuffer {
  //  static_assert(std::is_base_of<Semaphore, S>::value_type == true ||
 //                     std::is_base_of<Semaphore, S>::value_type == true,
//                  "Error: class must be either Semaphore or SPSCQueue");

  private:
    void *_shm;
    const long int _max_num_elems, _elem_size; // elements size in bytes
    long int _buff_size;                       // buffer size in bytes
    long int *_first_elem = nullptr, *_last_elem = nullptr;
    const std::string _shm_name, _first_elem_name, _last_elem_name;
    Semaphore _mutex, _sem_num_elems, _sem_num_empty;

  public:
    CircularBuffer(const std::string &shm_name, const long int max_num_elems,
                   const long int elem_size,
                   const std::string &workflow_name = get_capio_workflow_name())
        : _max_num_elems(max_num_elems), _elem_size(elem_size),
          _buff_size(_max_num_elems * _elem_size), _shm_name(workflow_name + "_" + shm_name),
          _first_elem_name(workflow_name + SHM_FIRST_ELEM + shm_name),
          _last_elem_name(workflow_name + SHM_LAST_ELEM + shm_name),
          _mutex(workflow_name + SHM_MUTEX_PREFIX + shm_name, 1),
          _sem_num_elems(workflow_name + SHM_SEM_ELEMS + shm_name, 0),
          _sem_num_empty(workflow_name + SHM_SEM_EMPTY + shm_name, max_num_elems) {
        START_LOG(capio_syscall(SYS_gettid),
                  "[circular_buffer] call(shm_name=%s, _max_num_elems=%ld, elem_size=%ld, "
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

    CircularBuffer(const CircularBuffer &)            = delete;
    CircularBuffer &operator=(const CircularBuffer &) = delete;

    ~CircularBuffer() {
        START_LOG(capio_syscall(SYS_gettid), "[circular_buffer] call()");
        SHM_DESTROY_CHECK(_shm_name.c_str());
        SHM_DESTROY_CHECK(_first_elem_name.c_str());
        SHM_DESTROY_CHECK(_last_elem_name.c_str());
    }

    inline void write(const T *data) {
        START_LOG(capio_syscall(SYS_gettid), "[circular_buffer] call(data=0x%08x)", data);

        std::lock_guard<S> lg(_mutex);

        _sem_num_empty.lock();

        memcpy((T *) _shm + *_last_elem, data, _elem_size);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;
        LOG("[circular_buffer] Wrote '%s' on %s", data, this->_shm_name.c_str());

        _sem_num_elems.unlock();
    }

    inline void read(T *buff_rcv) {
        START_LOG(capio_syscall(SYS_gettid), "[circular_buffer] call(buff_rcv=0x%08x)", buff_rcv);

        std::lock_guard<S> lg(_mutex);

        _sem_num_elems.lock();

        memcpy((T *) buff_rcv, ((T *) _shm) + *_first_elem, _elem_size);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;
        LOG("[circular_buffer] Received '%s' on %s", buff_rcv, this->_shm_name.c_str());

        _sem_num_empty.unlock();
    }
};

#endif // CAPIO_COMMON_CIRCULAR_BUFFER_HPP
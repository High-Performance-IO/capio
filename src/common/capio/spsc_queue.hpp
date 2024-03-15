#ifndef CAPIO_COMMON_SPSC_QUEUE_HPP
#define CAPIO_COMMON_SPSC_QUEUE_HPP

#include <semaphore.h>

#include "capio/shm.hpp"

/*
 * Multi-producer and multi-consumer circular buffer.
 * Each element of the circular buffer has the same size.
 * The items written to the SPSCqueue are only char (bytes)
 */

class _SPSCQueue {
  public:
    _SPSCQueue(std::string name, unsigned int init_value) {
        START_LOG(capio_syscall(SYS_gettid), "call(name=%s, initial_value=%d)", name.c_str(),
                  init_value);
    }
    ~_SPSCQueue() {}
    void lock() {}
    void unlock() {}
};

class SPSCQueue {
  private:
    void *_shm;
    const long int _max_num_elems;
    const long int _elem_size; // elements size in bytes
    long int _buff_size;       // buffer size in bytes
    long int *_first_elem;
    long int *_last_elem;
    const std::string _shm_name, _first_elem_name, _last_elem_name, _sem_num_elem_names,
        _sem_num_empty_name;
    sem_t *_sem_num_elems;
    sem_t *_sem_num_empty;

  public:
    SPSCQueue(const std::string &shm_name, const long int max_num_elems, const long int elem_size,
              std::string workflow_name)
        : _max_num_elems(max_num_elems), _elem_size(elem_size),
          _shm_name(workflow_name + "_" + shm_name),
          _first_elem_name(workflow_name + SHM_FIRST_ELEM + shm_name),
          _last_elem_name(workflow_name + SHM_LAST_ELEM + shm_name),
          _sem_num_elem_names(workflow_name + SHM_SEM_ELEMS + shm_name),
          _sem_num_empty_name(workflow_name + SHM_SEM_EMPTY + shm_name) {

        START_LOG(
            capio_syscall(SYS_gettid),
            "[SPSCqueue] call(shm_name=%s, _max_num_elems=%ld, elem_size=%ld, workflow_name:%s)",
            shm_name.c_str(), max_num_elems, elem_size, workflow_name.c_str());

        _buff_size  = _max_num_elems * _elem_size;
        _first_elem = (long int *) create_shm(_first_elem_name, sizeof(long int));
        _last_elem  = (long int *) create_shm(_last_elem_name, sizeof(long int));
        _shm        = get_shm_if_exist(_shm_name);
        if (_shm == nullptr) {
            *_first_elem = 0;
            *_last_elem  = 0;
            _shm         = create_shm(_shm_name, _buff_size);
        }

        _sem_num_elems =
            sem_open((_sem_num_elem_names).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR,
                     0); // check the flags
        if (_sem_num_elems == SEM_FAILED) {
            ERR_EXIT("sem_open _sem_num_elems %s", _shm_name.c_str());
        }
        _sem_num_empty =
            sem_open((_sem_num_empty_name).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR,
                     _max_num_elems); // check the flags
        if (_sem_num_empty == SEM_FAILED) {
            ERR_EXIT("sem_open _sem_num_empty %s", _shm_name.c_str());
        }
    }

    SPSCQueue(const SPSCQueue &)            = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;

    ~SPSCQueue() {
        START_LOG(capio_syscall(SYS_gettid), "[SPSCqueue] call()");
        SHM_DESTROY_CHECK(_shm_name.c_str());
        SHM_DESTROY_CHECK(_first_elem_name.c_str());
        SHM_DESTROY_CHECK(_last_elem_name.c_str());
        SHM_DESTROY_CHECK(_sem_num_elem_names.c_str());
        SHM_DESTROY_CHECK(_sem_num_empty_name.c_str());
        sem_close(_sem_num_elems);
        sem_close(_sem_num_empty);
    }

    void write(const char *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "[SPSCqueue] call(data=0x%08x, num_bytes=%ld)", data,
                  num_bytes);

        if (num_bytes > _elem_size) {
            ERR_EXIT("[SPSCqueue] circular buffer %s write error: num_bytes > _elem_size",
                     _shm_name.c_str());
        }

        SEM_WAIT_CHECK(_sem_num_empty, _sem_num_empty_name.c_str());
        memcpy((char *) _shm + *_last_elem, data, num_bytes);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;
        SEM_POST_CHECK(_sem_num_elems, _sem_num_elem_names.c_str());
    }

    inline void write(const char *data) {
        START_LOG(capio_syscall(SYS_gettid), "[SPSCqueue] call()");
        write(data, _elem_size);
    }

    void read(char *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "[SPSCqueue] call(buff_rcv=0x%08x, num_bytes=%ld)",
                  buff_rcv, num_bytes);

        if (num_bytes > _elem_size) {
            ERR_EXIT("circular buffer %s read error: num_bytes > _elem_size", _shm_name.c_str());
        }

        SEM_WAIT_CHECK(_sem_num_elems, _sem_num_elem_names.c_str());
        memcpy((char *) buff_rcv, ((char *) _shm) + *_first_elem, num_bytes);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;
        SEM_POST_CHECK(_sem_num_empty, _sem_num_empty_name.c_str());
    }

    inline void read(char *buf_rcv) {
        START_LOG(capio_syscall(SYS_gettid), "[SPSCqueue] call()");
        read(buf_rcv, _elem_size);
    }
};

#endif // CAPIO_COMMON_SPSC_QUEUE_HPP

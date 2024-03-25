#ifndef CAPIO_COMMON_SPSC_QUEUE_HPP
#define CAPIO_COMMON_SPSC_QUEUE_HPP

#include <semaphore.h>

#include "capio/shm.hpp"

/*
 * Multi-producer and multi-consumer circular buffer.
 * Each element of the circular buffer has the same size.
 */

template <class T> class SPSCQueue {
  private:
    void *_shm;
    const long int _max_num_elems;
    const long int _elem_size; // elements size in bytes
    long int _buff_size;       // buffer size in bytes
    long int *_first_elem;
    long int *_last_elem;
    const std::string _shm_name, _mutex_name, _first_elem_name, _last_elem_name,
        _sem_num_elem_names, _sem_num_empty_name;
    sem_t *_sem_num_elems;
    sem_t *_sem_num_empty;
    struct timespec sem_timeout_struct;
    int _sem_retries;

    // TODO: find length required to wait for semaphores!
    inline int sem_wait_for(sem_t *sem) {
        START_LOG(gettid(), "call()");

        int retries = 0;
        int retval;
        while (retries < _sem_retries && (retval = sem_timedwait(sem, &sem_timeout_struct)) != 0) {
            retries++;
        }

        if (retries < _sem_retries) {
            return retval;
        }
        ERR_EXIT("FATAL: semaphore timeout reached from spsc_queue. EXIT APP");
    }

  public:
    SPSCQueue(const std::string &shm_name, const long int max_num_elems, const long int elem_size,
              long int sem_timeout, int sem_retries,
              std::string workflow_name = get_capio_workflow_name())
        : _max_num_elems(max_num_elems), _elem_size(elem_size),
          _shm_name(workflow_name + "_" + shm_name),
          _first_elem_name(workflow_name + "_first_elem_" + shm_name),
          _last_elem_name(workflow_name + "_last_elem_" + shm_name),
          _mutex_name(workflow_name + "_mutex_" + shm_name),
          _sem_num_elem_names(workflow_name + "_sem_num_elems_" + shm_name),
          _sem_num_empty_name(workflow_name + "_sem_num_empty_" + shm_name) {
        START_LOG(capio_syscall(SYS_gettid),
                  "call(shm_name=%s, _max_num_elems=%ld, elem_size=%ld, "
                  "sem_timeout=%ld, sem_retries=%d, workflow_name:%s)",
                  shm_name.c_str(), _max_num_elems, elem_size, sem_timeout, sem_retries,
                  workflow_name.data());

        sem_timeout_struct.tv_nsec = sem_timeout;
        sem_timeout_struct.tv_sec  = 1;
        _buff_size                 = _max_num_elems * _elem_size;
        _first_elem                = (long int *) create_shm(_first_elem_name, sizeof(long int));
        _last_elem                 = (long int *) create_shm(_last_elem_name, sizeof(long int));
        _shm                       = get_shm_if_exist(_shm_name);
        if (_shm == nullptr) {
            *_first_elem = 0;
            *_last_elem  = 0;
            _shm         = create_shm(shm_name, _buff_size);
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

    SPSCQueue(const SPSCQueue &) = delete;

    SPSCQueue &operator=(const SPSCQueue &) = delete;

    ~SPSCQueue() {
        sem_close(_sem_num_elems);
        sem_close(_sem_num_empty);
    }

    void free_shm() {
        shm_unlink(_shm_name.c_str());
        shm_unlink((_first_elem_name).c_str());
        shm_unlink((_last_elem_name).c_str());
        sem_unlink((_sem_num_elem_names).c_str());
        sem_unlink((_sem_num_empty_name).c_str());
    }

    void write(const T *data) {
        START_LOG(capio_syscall(SYS_gettid), "call(data=0x%08x)", data);

        if (sem_wait(_sem_num_empty) == -1) {
            ERR_EXIT("sem_wait _sem_num_empty");
        }

        memcpy((char *) _shm + *_last_elem, data, _elem_size);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;

        if (sem_post(_sem_num_elems) == -1) {
            ERR_EXIT("sem_post _sem_num_elems");
        }
    }

    void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(data=0x%08x, num_bytes=%ld)", data, num_bytes);

        if (num_bytes > _elem_size) {
            ERR_EXIT("circular buffer %s write error: num_bytes > _elem_size", _shm_name.c_str());
        }

        if (sem_wait(_sem_num_empty) == -1) {
            ERR_EXIT("sem_wait _sem_num_empty");
        }

        memcpy((char *) _shm + *_last_elem, data, num_bytes);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;

        if (sem_post(_sem_num_elems) == -1) {
            ERR_EXIT("sem_post _sem_num_elems");
        }
    }

    void read(T *buff_rcv) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x)", buff_rcv);

        if (sem_wait(_sem_num_elems) == -1) {
            ERR_EXIT("sem_wait _sem_num_elems");
        }

        memcpy((char *) buff_rcv, ((char *) _shm) + *_first_elem, _elem_size);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;

        if (sem_post(_sem_num_empty) == -1) {
            ERR_EXIT("sem_post _sem_num_empty");
        }
    }

    /*
     * It reads only the firsts num_bytes bytes of the buffer element
     */

    void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x, num_bytes=%ld)", buff_rcv,
                  num_bytes);

        if (num_bytes > _elem_size) {
            ERR_EXIT("circular buffer %s read error: num_bytes > _elem_size", _shm_name.c_str());
        }

        if (sem_wait(_sem_num_elems) == -1) {
            ERR_EXIT("sem_wait _sem_num_elems");
        }

        memcpy((char *) buff_rcv, ((char *) _shm) + *_first_elem, num_bytes);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;

        if (sem_post(_sem_num_empty) == -1) {
            ERR_EXIT("sem_post _sem_num_empty");
        }
    }
};

#endif // CAPIO_COMMON_SPSC_QUEUE_HPP

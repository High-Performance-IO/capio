#ifndef CAPIO_QUEUE_HPP
#define CAPIO_QUEUE_HPP

#include <iostream>
#include <mutex>

#include <semaphore.h>

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"
#include "capio/shm.hpp"

#include "capio/channels/shm_channels.hpp"

template <class T, class Mutex, class TransferChannel> class Queue {
  private:
    const long int _elem_size;
    std::string _queue_name;
    Mutex _mutex;
    NamedSemaphore _sem_num_elems, _sem_num_empty;
    TransferChannel *channel;

  public:
    Queue(const std::string &shm_name, const long int max_num_elems, const long int elem_size,
          const std::string &workflow_name = get_capio_workflow_name())
        : _elem_size(elem_size), _mutex(workflow_name + SHM_MUTEX_PREFIX + shm_name, 1),
          _queue_name(workflow_name + "_" + shm_name),
          _sem_num_elems(workflow_name + SHM_SEM_ELEMS + shm_name, 0),
          _sem_num_empty(workflow_name + SHM_SEM_EMPTY + shm_name, max_num_elems) {
        START_LOG(capio_syscall(SYS_gettid),
                  "call(shm_name=%s, _max_num_elems=%ld, elem_size=%ld, workflow_name=%s)",
                  shm_name.data(), max_num_elems, elem_size, workflow_name.data());
        channel = new TransferChannel(elem_size * max_num_elems, _queue_name,
                                      workflow_name + SHM_FIRST_ELEM + shm_name,
                                      workflow_name + SHM_LAST_ELEM + shm_name, elem_size);
    }

    Queue(const Queue &)            = delete;
    Queue &operator=(const Queue &) = delete;
    ~Queue() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        delete channel;
    }

    inline auto get_name() { return channel->get_name(); }

    inline void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(data=0x%08x)", data);

        _sem_num_empty.lock();
        std::lock_guard<Mutex> lg(_mutex);
        channel->write(data, num_bytes);
        _sem_num_elems.unlock();
    }

    inline void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call(buff_rcv=0x%08x)", buff_rcv);

        _sem_num_elems.lock();
        std::lock_guard<Mutex> lg(_mutex);
        channel->read(buff_rcv, num_bytes);
        _sem_num_empty.unlock();
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
template <class T> using CircularBuffer = Queue<T, NamedSemaphore, SHMChannel<T>>;

// Single Producer Single Consumer queue
using SPSCQueue = Queue<char, NoLock, SHMChannel<char>>;
#endif // CAPIO_QUEUE_HPP

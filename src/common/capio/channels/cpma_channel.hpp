#ifndef CAPIO_CPMA_CHANNEL_HPP
#define CAPIO_CPMA_CHANNEL_HPP

template <typename T> class CPMAChannel {
  protected:
    void *_shm;
    long int *_first_elem = nullptr, *_last_elem = nullptr;
    const std::string _shm_name, _first_elem_name, _last_elem_name;
    const long int _elem_size; // elements size in bytes
    long int _buff_size;       // buffer size in bytes

  public:
    explicit CPMAChannel(long int buff_size, std::string &shm_name, std::string first_elem_name,
                         std::string last_elem_name, const long int elem_size)
        : _shm_name(std::move(shm_name)), _first_elem_name(std::move(first_elem_name)),
          _last_elem_name(std::move(last_elem_name)), _elem_size(elem_size), _buff_size(buff_size) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        _first_elem = (long int *) create_shm(_first_elem_name, sizeof(long int));
        _last_elem  = (long int *) create_shm(_last_elem_name, sizeof(long int));
        _shm        = get_shm_if_exist(_shm_name);
        if (_shm == nullptr) {
            *_first_elem = 0;
            *_last_elem  = 0;
            _shm         = create_shm(_shm_name, _buff_size);
        }
    }

    inline auto get_name() { return this->_shm_name; }

    ~CPMAChannel() {
        START_LOG(capio_syscall(SYS_gettid),
                  "call(_shm_name=%s, _first_elem_name=%s, _last_elem_name=%s)", _shm_name.c_str(),
                  _first_elem_name.c_str(), _last_elem_name.c_str());
        SHM_DESTROY_CHECK(_shm_name.c_str());
        SHM_DESTROY_CHECK(_first_elem_name.c_str());
        SHM_DESTROY_CHECK(_last_elem_name.c_str());
    }

    inline void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (num_bytes > _elem_size) {
            ERR_EXIT("Queue %s write error: num_bytes > _elem_size", _shm_name.c_str());
        }

        memcpy((char *) _shm + *_last_elem, data, num_bytes);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;
        LOG("Wrote '%s' (%d) on %s", data, data, _shm_name.c_str());
    }

    inline void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        memcpy((char *) buff_rcv, ((char *) _shm) + *_first_elem, num_bytes);

        if (num_bytes > _elem_size) {
            ERR_EXIT("Queue %s read error: num_bytes > _elem_size", _shm_name.c_str());
        }

        *_first_elem = (*_first_elem + _elem_size) % _buff_size;
        LOG("Read '%s' (%d) on %s", buff_rcv, buff_rcv, _shm_name.c_str());
    }
};

#endif // CAPIO_CPMA_CHANNEL_HPP

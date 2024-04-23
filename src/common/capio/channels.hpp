#ifndef CAPIO_CHANNELS_HPP
#define CAPIO_CHANNELS_HPP
template <typename T> class SHMChannel {
  public:
    inline void init(void *_shm, long int *_last_elem, long int *_first_elem,
                            long int _buff_size, const std::string &_shm_name,
                            const std::string &_first_elem_name,
                            const std::string &_last_elem_name) {
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

    inline void write(void *_shm, const T *data, long int num_bytes, long int *_last_elem,
                             long int _elem_size, long int _buff_size,
                             const std::string &_shm_name) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        memcpy((char *) _shm + *_last_elem, data, num_bytes);
        *_last_elem = (*_last_elem + _elem_size) % _buff_size;
        LOG("Wrote '%s' (%d) on %s", data, data, _shm_name.c_str());
    }

    inline void read(T *buff_rcv, long int num_bytes, void *_shm, long int *_first_elem,
                            long int _elem_size, long int _buff_size,
                            const std::string &_shm_name) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        memcpy((char *) buff_rcv, ((char *) _shm) + *_first_elem, num_bytes);
        *_first_elem = (*_first_elem + _elem_size) % _buff_size;
        LOG("Read '%s' (%d) on %s", buff_rcv, buff_rcv, _shm_name.c_str());
    }
};
#endif // CAPIO_CHANNELS_HPP

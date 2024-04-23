#ifndef CAPIO_CPMA_CHANNEL_HPP
#define CAPIO_CPMA_CHANNEL_HPP

template <typename T> class CPMAChannel {
  protected:
  public:
    explicit CPMAChannel(long int buff_size, std::string &shm_name, std::string first_elem_name,
                         std::string last_elem_name, const long int elem_size) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
    }

    inline auto get_name() { return this->_shm_name; }

    ~CPMAChannel() { START_LOG(capio_syscall(SYS_gettid), "call()"); }

    inline void write(const T *data, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
    }

    inline void read(T *buff_rcv, long int num_bytes) {
        START_LOG(capio_syscall(SYS_gettid), "call()");
    }
};

#endif // CAPIO_CPMA_CHANNEL_HPP

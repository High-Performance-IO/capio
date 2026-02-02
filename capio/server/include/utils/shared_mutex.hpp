#ifndef CAPIO_SHARED_MUTEX_HPP
#define CAPIO_SHARED_MUTEX_HPP

#include <shared_mutex>

template <typename SharedMutex> class shared_lock_guard {
  public:
    explicit shared_lock_guard(SharedMutex &m) : mutex_(m) { mutex_.lock_shared(); }

    ~shared_lock_guard() { mutex_.unlock_shared(); }

    shared_lock_guard(const shared_lock_guard &)            = delete;
    shared_lock_guard &operator=(const shared_lock_guard &) = delete;

  private:
    SharedMutex &mutex_;
};

#endif // CAPIO_SHARED_MUTEX_HPP

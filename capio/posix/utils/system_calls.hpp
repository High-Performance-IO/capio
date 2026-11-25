#ifndef CAPIO_POSIX_UTILS_SYSTEM_CALLS_HPP
#define CAPIO_POSIX_UTILS_SYSTEM_CALLS_HPP

/**
 * @brief The system calls that the classes of the client make, through syscall_no_intercept.
 *
 * Implemented in src/system_calls.cpp. The unit tests do not compile that file: they implement
 * these methods themselves, as mocks.
 */
class SystemCalls final {
  public:
    SystemCalls() = delete;

    /// @return what close(2) returned: 0, or -errno
    static long close(int fd);
};

#endif // CAPIO_POSIX_UTILS_SYSTEM_CALLS_HPP

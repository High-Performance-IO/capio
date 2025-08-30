#ifndef DISTRIBUTEDSEMAPHORE_HPP
#define DISTRIBUTEDSEMAPHORE_HPP

#include <capio/logger.hpp>

/**
 * @brief Class to provide mutually exclusive access to files on distributed file systems by
 * providing file based locking
 */
class DistributedSemaphore {
    std::string name;
    timespec sleep{};
    bool locked;
    int fp;

    void lock();

    void unlock() const;

  public:
    /**
     * @brief Construct a new Distributed Semaphore object. At instantiation of the object, the
     * _lock() method is created this locking the resource. If the locking fails, a new attempt is
     * done after sleep_time nanoseconds
     *
     * @param locking The file that is the target to mutual exclusive access
     * @param sleep_time Sleep time between lock tries in nano seconds
     */
    DistributedSemaphore(std::string locking, int sleep_time);

    /**
     * @brief Destroy the Distributed Semaphore object thus unlocking the resource
     *
     */
    ~DistributedSemaphore();
};
#endif // DISTRIBUTEDSEMAPHORE_HPP
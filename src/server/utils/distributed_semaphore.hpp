#ifndef DISTRIBUTEDSEMAPHORE_HPP
#define DISTRIBUTEDSEMAPHORE_HPP

/**
 * @brief Class to provide mutually exclusive access to files on distributed file systems by
 * providing file based locking
 */
class DistributedSemaphore {
    std::string name;
    timespec sleep{};
    bool locked;
    int fp;

    void lock() {
        START_LOG(gettid(), "call(locking=%s)", name.c_str());
        if (!locked) {
            while (fp == -1) {
                nanosleep(&sleep, nullptr);
                fp = open(name.c_str(), O_EXCL | O_CREAT | O_WRONLY, 0777);
            }
            LOG("Locked %s", name.c_str());
            if (write(fp, capio_global_configuration->node_name,
                      strlen(capio_global_configuration->node_name)) == -1) {
                ERR_EXIT("Unable to insert lock holder %s on lock file %s",
                         capio_global_configuration->node_name, name.c_str());
            }
        }
        LOG("Completed spinlock on lock file %s", name.c_str());
        locked = true;
    }

    void unlock() const {
        START_LOG(gettid(), "call(unlocking=%s)", name.c_str());
        if (locked) {
            close(fp);
            unlink(name.c_str());
        }
        LOG("Unlocked %s", name.c_str());
    }

  public:
    /**
     * @brief Construct a new Distributed Semaphore object. At instantiation of the object, the
     * _lock() method is created this locking the resource. If the locking fails, a new attempt is
     * done after sleep_time nanoseconds
     *
     * @param locking The file that is the target to mutual exclusive access
     * @param sleep_time Sleep time between lock tries in nano seconds
     */
    DistributedSemaphore(std::string locking, int sleep_time)
        : name(locking), locked(false), fp(-1) {
        START_LOG(gettid(), "call(locking=%s, sleep_time=%ld)", name.c_str(), sleep_time);
        sleep.tv_nsec = sleep_time;

        this->lock();
    }

    /**
     * @brief Destroy the Distributed Semaphore object thus unlocking the resource
     *
     */
    ~DistributedSemaphore() {
        START_LOG(gettid(), "call()");
        if (locked) {
            this->unlock();
        }
    }
};
#endif // DISTRIBUTEDSEMAPHORE_HPP

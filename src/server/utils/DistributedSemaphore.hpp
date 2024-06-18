#include <utility>

#ifndef DISTRIBUTEDSEMAPHORE_HPP
#define DISTRIBUTEDSEMAPHORE_HPP
class DistributedSemaphore {
  private:
    std::string name;
    struct timespec sleep {};
    bool locked;
    int fp;

  public:
    DistributedSemaphore(std::string locking, int sleep_time)
        : name(std::move(locking)), locked(false), fp(-1) {
        sleep.tv_nsec = sleep_time;
    }

    ~DistributedSemaphore() {
        if (locked) {
            this->unlock();
        }
    }

    void lock() {
        if (!locked) {
            while ((fp = open(name.c_str(), O_EXCL | O_CREAT, 0777)) == -1) {
                nanosleep(&sleep, nullptr);
            }
        }

        locked = true;
    }

    void unlock() const {
        if (locked) {
            close(fp);
            unlink(name.c_str());
        }
    }
};
#endif // DISTRIBUTEDSEMAPHORE_HPP

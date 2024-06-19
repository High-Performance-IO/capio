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
        START_LOG(gettid(), "call(locking=%s, sleep_time=%ld)", locking.c_str(), sleep_time);
        sleep.tv_nsec = sleep_time;
    }

    ~DistributedSemaphore() {
        START_LOG(gettid(), "call()");
        if (locked) {
            this->unlock();
        }
    }

    void lock() {
        START_LOG(gettid(), "call(locking=%s)", name.c_str());
        if (!locked) {
            while ((fp = open(name.c_str(), O_EXCL | O_CREAT, 0777)) == -1) {
                nanosleep(&sleep, nullptr);
            }
            write(fp, node_name, strlen(node_name));
        }
        LOG("Locked %s", name.c_str());
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
};
#endif // DISTRIBUTEDSEMAPHORE_HPP

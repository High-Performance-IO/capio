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
        : name(locking), locked(false), fp(-1) {
        START_LOG(gettid(), "call(locking=%s, sleep_time=%ld)", name.c_str(), sleep_time);
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
            while (fp == -1) {
                nanosleep(&sleep, nullptr);
                fp = open(name.c_str(), O_EXCL | O_CREAT | O_WRONLY, 0777);
            }
            LOG("Locked %s", name.c_str());
            if (write(fp, node_name, strlen(node_name)) == -1) {
                ERR_EXIT("Unable to insert lock holder %s on lock file %s", node_name,
                         name.c_str());
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
};
#endif // DISTRIBUTEDSEMAPHORE_HPP

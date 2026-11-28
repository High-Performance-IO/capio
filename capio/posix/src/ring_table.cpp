#include <sys/syscall.h>

#if defined(SYS_io_uring_setup)

#include <new>
#include <pthread.h>

#include "utils/uring.hpp"

std::mutex &RingTable::mutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<CapioRing> RingTable::find(const int fd) {
    if (_count.load(std::memory_order_acquire) == 0) {
        return nullptr;
    }
    const std::lock_guard lock(mutex());
    const auto it = _rings.find(fd);
    return it == _rings.end() ? nullptr : it->second;
}

void RingTable::add(const int fd, std::shared_ptr<CapioRing> ring) {
    std::shared_ptr<CapioRing> stale; // freed after the lock: its destructor makes system calls
    const std::lock_guard lock(mutex());
    if (const auto it = _rings.find(fd); it != _rings.end()) {
        stale      = std::move(it->second);
        it->second = std::move(ring);
        return;
    }
    _rings.emplace(fd, std::move(ring));
    _count.store(_rings.size(), std::memory_order_release);
}

bool RingTable::remove(const int fd) {
    std::shared_ptr<CapioRing> ring; // freed after the lock, as in add
    const std::lock_guard lock(mutex());
    const auto it = _rings.find(fd);
    if (it == _rings.end()) {
        return false;
    }
    ring = std::move(it->second);
    _rings.erase(it);
    _count.store(_rings.size(), std::memory_order_release);
    return true;
}

std::vector<int> RingTable::fdsBetween(const unsigned first, const unsigned last) {
    std::vector<int> fds;
    if (_count.load(std::memory_order_acquire) == 0) {
        return fds;
    }
    const std::lock_guard lock(mutex());
    for (const auto &[fd, ring] : _rings) {
        if (static_cast<unsigned>(fd) >= first && static_cast<unsigned>(fd) <= last) {
            fds.push_back(fd);
        }
    }
    return fds;
}

void RingTable::protectAcrossFork() {
    // As FileDescriptorTable::protectAcrossFork. The lock of each ring is not taken before the
    // fork: io_uring_enter holds it while it closes a file, which takes the lock of the table. So
    // the child, which has only one thread, gets a new one in its place.
    pthread_atfork([] { mutex().lock(); }, [] { mutex().unlock(); },
                   [] {
                       new (&mutex()) std::mutex();
                       for (const auto &[fd, ring] : ring_table()._rings) {
                           new (&ring->lock) std::mutex();
                       }
                   });
}

RingTable &ring_table() {
    static auto *const table = new RingTable();
    return *table;
}

#endif // SYS_io_uring_setup

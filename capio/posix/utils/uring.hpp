#ifndef CAPIO_POSIX_UTILS_URING_HPP
#define CAPIO_POSIX_UTILS_URING_HPP

#if defined(SYS_io_uring_setup)

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <linux/io_uring.h>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <unordered_map>

#include "calf/SyscallLogger.h"
#include "common/syscall.hpp"

// Setup flags newer than some kernel headers; define them (stable ABI values).
#ifndef IORING_SETUP_SUBMIT_ALL
#define IORING_SETUP_SUBMIT_ALL (1U << 7)
#endif
#ifndef IORING_SETUP_COOP_TASKRUN
#define IORING_SETUP_COOP_TASKRUN (1U << 8)
#endif
#ifndef IORING_SETUP_TASKRUN_FLAG
#define IORING_SETUP_TASKRUN_FLAG (1U << 9)
#endif
#ifndef IORING_SETUP_SINGLE_ISSUER
#define IORING_SETUP_SINGLE_ISSUER (1U << 12)
#endif
#ifndef IORING_SETUP_DEFER_TASKRUN
#define IORING_SETUP_DEFER_TASKRUN (1U << 13)
#endif
#ifndef IORING_SETUP_NO_SQARRAY
#define IORING_SETUP_NO_SQARRAY (1U << 16)
#endif
#ifndef IOSQE_CQE_SKIP_SUCCESS
#define IOSQE_CQE_SKIP_SUCCESS (1U << 6)
#endif
#ifndef IORING_FEAT_CQE_SKIP
#define IORING_FEAT_CQE_SKIP (1U << 11)
#endif

// The hidden kernel ring: it serves every SQE that does not concern CAPIO.
struct KernelRing {
    int fd              = -1;
    uint32_t features   = 0;
    void *sq_ring       = nullptr;
    size_t sq_size      = 0;
    void *cq_ring       = nullptr;
    size_t cq_size      = 0;
    io_uring_sqe *sqes  = nullptr;
    size_t sqes_size    = 0;
    uint32_t sq_entries = 0;
    uint32_t *sq_head = nullptr, *sq_tail = nullptr, *sq_mask = nullptr, *sq_array = nullptr;
    uint32_t *cq_head = nullptr, *cq_tail = nullptr, *cq_mask = nullptr;
    io_uring_cqe *cqes = nullptr;
    uint32_t pending   = 0; // copied into the SQ, not yet submitted
    uint32_t inflight  = 0; // submitted, final CQE not yet copied to the app
};

// CAPIO's ring: a memfd that the app maps with the kernel's mmap and CAPIO maps
// once, MAP_SHARED (a forked child shares it). SQ, CQ and SQEs sit at their
// io_uring offsets; the file is sparse.
struct CapioRing {
    uint32_t sq_entries;
    uint32_t cq_entries;
    bool no_sqarray = false;

    void *sq_ring       = nullptr;
    size_t sq_ring_size = 0;
    void *cq_ring       = nullptr;
    size_t cq_ring_size = 0;
    io_uring_sqe *sqes  = nullptr;
    size_t sqes_size    = 0;

    uint32_t *sq_head = nullptr, *sq_tail = nullptr, *sq_mask = nullptr, *sq_dropped = nullptr;
    uint32_t *sq_flags = nullptr; // IORING_SQ_CQ_OVERFLOW hint (see uring_kernel.hpp)
    uint32_t *sq_array = nullptr;
    uint32_t *cq_head = nullptr, *cq_tail = nullptr, *cq_mask = nullptr, *cq_ring_entries = nullptr;
    io_uring_cqe *cqes = nullptr;

    KernelRing kernel;
    std::deque<io_uring_cqe> overflow;
    int eventfd        = -1;
    bool eventfd_async = false;
    uint32_t waiters   = 0;
    std::mutex lock;

    ~CapioRing();
};

inline size_t uring_align_up(size_t n, size_t alignment) {
    return (n + alignment - 1) & ~(alignment - 1);
}

inline void uring_unmap(CapioRing &ring) {
    if (ring.sq_ring != nullptr) {
        capio_syscall(SYS_munmap, ring.sq_ring, ring.sq_ring_size);
        ring.sq_ring = nullptr;
    }
    if (ring.cq_ring != nullptr) {
        capio_syscall(SYS_munmap, ring.cq_ring, ring.cq_ring_size);
        ring.cq_ring = nullptr;
    }
    if (ring.sqes != nullptr) {
        capio_syscall(SYS_munmap, ring.sqes, ring.sqes_size);
        ring.sqes = nullptr;
    }
}

inline bool uring_layout(CapioRing &ring, io_uring_params *params, int memfd) {
    START_LOG(capio_syscall(SYS_gettid), "call(sq_entries=%u, cq_entries=%u, memfd=%d)",
              ring.sq_entries, ring.cq_entries, memfd);

    io_sqring_offsets &sq = params->sq_off;
    io_cqring_offsets &cq = params->cq_off;
    sq                    = {};
    cq                    = {};
    uint32_t off          = 0;
    auto next             = [&off](size_t bytes) {
        const uint32_t at = off;
        off += static_cast<uint32_t>(bytes);
        return at;
    };

    // Offsets are relative to the start of their own region.
    sq.head           = next(sizeof(uint32_t));
    sq.tail           = next(sizeof(uint32_t));
    sq.ring_mask      = next(sizeof(uint32_t));
    sq.ring_entries   = next(sizeof(uint32_t));
    sq.flags          = next(sizeof(uint32_t));
    sq.dropped        = next(sizeof(uint32_t));
    sq.array          = next(sizeof(uint32_t) * ring.sq_entries);
    ring.sq_ring_size = uring_align_up(off, 4096);

    off               = 0;
    cq.head           = next(sizeof(uint32_t));
    cq.tail           = next(sizeof(uint32_t));
    cq.ring_mask      = next(sizeof(uint32_t));
    cq.ring_entries   = next(sizeof(uint32_t));
    cq.overflow       = next(sizeof(uint32_t));
    cq.flags          = next(sizeof(uint32_t));
    off               = static_cast<uint32_t>(uring_align_up(off, alignof(io_uring_cqe)));
    cq.cqes           = next(sizeof(io_uring_cqe) * ring.cq_entries);
    ring.cq_ring_size = uring_align_up(off, 4096);

    ring.sqes_size = uring_align_up(sizeof(io_uring_sqe) * ring.sq_entries, 4096);

    // Raw syscalls: CAPIO's own memory must not go through its hooks.
    if (capio_syscall(SYS_ftruncate, memfd, IORING_OFF_SQES + ring.sqes_size) < 0) {
        LOG("ftruncate of ring memfd failed");
        return false;
    }
    auto map = [memfd](size_t size, unsigned long long offset) {
        const long mem = capio_syscall(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                                       memfd, offset);
        return mem < 0 ? nullptr : reinterpret_cast<void *>(mem);
    };
    ring.sq_ring = map(ring.sq_ring_size, IORING_OFF_SQ_RING);
    ring.cq_ring = map(ring.cq_ring_size, IORING_OFF_CQ_RING);
    ring.sqes    = static_cast<io_uring_sqe *>(map(ring.sqes_size, IORING_OFF_SQES));
    if (ring.sq_ring == nullptr || ring.cq_ring == nullptr || ring.sqes == nullptr) {
        LOG("mmap of ring memfd failed");
        uring_unmap(ring);
        return false;
    }

    auto sq_field = [&ring](uint32_t offset) {
        return reinterpret_cast<uint32_t *>(static_cast<char *>(ring.sq_ring) + offset);
    };
    auto cq_field = [&ring](uint32_t offset) {
        return reinterpret_cast<uint32_t *>(static_cast<char *>(ring.cq_ring) + offset);
    };
    ring.sq_head         = sq_field(sq.head);
    ring.sq_tail         = sq_field(sq.tail);
    ring.sq_mask         = sq_field(sq.ring_mask);
    ring.sq_dropped      = sq_field(sq.dropped);
    ring.sq_flags        = sq_field(sq.flags);
    ring.sq_array        = sq_field(sq.array);
    ring.cq_head         = cq_field(cq.head);
    ring.cq_tail         = cq_field(cq.tail);
    ring.cq_mask         = cq_field(cq.ring_mask);
    ring.cq_ring_entries = cq_field(cq.ring_entries);
    ring.cqes = reinterpret_cast<io_uring_cqe *>(static_cast<char *>(ring.cq_ring) + cq.cqes);

    *ring.sq_mask              = ring.sq_entries - 1;
    *sq_field(sq.ring_entries) = ring.sq_entries;
    *ring.cq_mask              = ring.cq_entries - 1;
    *ring.cq_ring_entries      = ring.cq_entries;

    LOG("uring_layout: sq_ring_size=%zu cq_ring_size=%zu sqes_size=%zu", ring.sq_ring_size,
        ring.cq_ring_size, ring.sqes_size);
    return true;
}

inline long uring_kernel_setup(CapioRing &ring) {
    START_LOG(capio_syscall(SYS_gettid), "call(sq_entries=%u, cq_entries=%u)", ring.sq_entries,
              ring.cq_entries);
    KernelRing &k = ring.kernel;
    io_uring_params p{};
    p.flags       = IORING_SETUP_CQSIZE;
    p.cq_entries  = ring.cq_entries;
    const long fd = capio_syscall(SYS_io_uring_setup, ring.sq_entries, &p);
    if (fd < 0) {
        LOG("kernel io_uring_setup failed: %ld", fd);
        return fd;
    }
    k.fd              = static_cast<int>(fd);
    k.features        = p.features;
    k.sq_entries      = p.sq_entries;
    k.sq_size         = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
    k.cq_size         = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
    k.sqes_size       = p.sq_entries * sizeof(io_uring_sqe);
    const bool single = p.features & IORING_FEAT_SINGLE_MMAP;
    if (single) {
        k.sq_size = k.cq_size = std::max(k.sq_size, k.cq_size);
    }
    auto map = [&k](size_t size, unsigned long long offset) {
        const long mem = capio_syscall(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE,
                                       MAP_SHARED | MAP_POPULATE, k.fd, offset);
        return mem < 0 ? nullptr : reinterpret_cast<void *>(mem);
    };
    k.sq_ring = map(k.sq_size, IORING_OFF_SQ_RING);
    k.cq_ring = single ? k.sq_ring : map(k.cq_size, IORING_OFF_CQ_RING);
    k.sqes    = static_cast<io_uring_sqe *>(map(k.sqes_size, IORING_OFF_SQES));
    if (k.sq_ring == nullptr || k.cq_ring == nullptr || k.sqes == nullptr) {
        LOG("mmap of the kernel ring failed");
        return -ENOMEM;
    }
    auto *sq   = static_cast<char *>(k.sq_ring);
    auto *cq   = static_cast<char *>(k.cq_ring);
    k.sq_head  = reinterpret_cast<uint32_t *>(sq + p.sq_off.head);
    k.sq_tail  = reinterpret_cast<uint32_t *>(sq + p.sq_off.tail);
    k.sq_mask  = reinterpret_cast<uint32_t *>(sq + p.sq_off.ring_mask);
    k.sq_array = reinterpret_cast<uint32_t *>(sq + p.sq_off.array);
    k.cq_head  = reinterpret_cast<uint32_t *>(cq + p.cq_off.head);
    k.cq_tail  = reinterpret_cast<uint32_t *>(cq + p.cq_off.tail);
    k.cq_mask  = reinterpret_cast<uint32_t *>(cq + p.cq_off.ring_mask);
    k.cqes     = reinterpret_cast<io_uring_cqe *>(cq + p.cq_off.cqes);
    return 0;
}

inline void uring_kernel_destroy(KernelRing &k) {
    if (k.cq_ring != nullptr && k.cq_ring != k.sq_ring) {
        capio_syscall(SYS_munmap, k.cq_ring, k.cq_size);
    }
    if (k.sq_ring != nullptr) {
        capio_syscall(SYS_munmap, k.sq_ring, k.sq_size);
    }
    if (k.sqes != nullptr) {
        capio_syscall(SYS_munmap, k.sqes, k.sqes_size);
    }
    if (k.fd >= 0) {
        capio_syscall(SYS_close, k.fd);
    }
    k = KernelRing{};
}

// The last reference frees the ring, so a close never unmaps it under an io_uring_enter.
inline CapioRing::~CapioRing() {
    uring_unmap(*this);
    uring_kernel_destroy(kernel);
}

inline uint32_t uring_cq_ready(const CapioRing &ring) {
    uint32_t head = __atomic_load_n(ring.cq_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE);
    return tail - head;
}

inline bool uring_cq_has_space(const CapioRing &ring) {
    return uring_cq_ready(ring) < *ring.cq_ring_entries;
}

inline void uring_write_cqe(CapioRing &ring, uint64_t user_data, int32_t res, uint32_t flags) {
    uint32_t tail     = *ring.cq_tail;
    io_uring_cqe &cqe = ring.cqes[tail & *ring.cq_mask];
    cqe.user_data     = user_data;
    cqe.res           = res;
    cqe.flags         = flags;
    // Release so the app sees the CQE fields before the advanced tail.
    __atomic_store_n(ring.cq_tail, tail + 1, __ATOMIC_RELEASE);
}

// While the CQ is full, CQEs wait in the overflow list, in order, as in the kernel.
inline void uring_post_cqe(CapioRing &ring, uint64_t user_data, int32_t res, uint32_t flags = 0) {
    if (!ring.overflow.empty() || !uring_cq_has_space(ring)) {
        ring.overflow.push_back(io_uring_cqe{user_data, res, flags});
        return;
    }
    uring_write_cqe(ring, user_data, res, flags);
}

// Per-process table of rings, keyed by ring fd. close runs for every file, so a
// process with no ring checks the ring count and never takes the lock.
inline std::unordered_map<int, std::shared_ptr<CapioRing>> *capio_rings;
inline std::mutex capio_rings_mutex;
inline std::atomic<int> capio_ring_count{0};

inline std::shared_ptr<CapioRing> get_capio_ring(int fd) {
    if (capio_ring_count.load(std::memory_order_acquire) == 0) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> lock(capio_rings_mutex);
    if (capio_rings == nullptr) {
        return nullptr;
    }
    auto it = capio_rings->find(fd);
    return it == capio_rings->end() ? nullptr : it->second;
}

// A stale ring left under the same fd number is replaced, not reused.
inline void add_capio_ring(int fd, std::shared_ptr<CapioRing> ring) {
    const std::lock_guard<std::mutex> lock(capio_rings_mutex);
    if (capio_rings == nullptr) {
        capio_rings = new std::unordered_map<int, std::shared_ptr<CapioRing>>();
    }
    if (capio_rings->insert_or_assign(fd, std::move(ring)).second) {
        capio_ring_count.fetch_add(1, std::memory_order_release);
    }
}

inline bool destroy_capio_ring(int fd) {
    std::shared_ptr<CapioRing> ring; // released after the table lock
    const std::lock_guard<std::mutex> lock(capio_rings_mutex);
    if (capio_rings == nullptr) {
        return false;
    }
    auto it = capio_rings->find(fd);
    if (it == capio_rings->end()) {
        return false;
    }

    ring = std::move(it->second);
    capio_rings->erase(it);
    capio_ring_count.fetch_sub(1, std::memory_order_release);
    if (capio_rings->empty()) {
        delete capio_rings;
        capio_rings = nullptr;
    }
    return true;
}

// Returns false when fd is not a ring.
inline bool uring_close(int fd, long *result) {
    if (get_capio_ring(fd) == nullptr) {
        return false;
    }
    *result = capio_syscall(SYS_close, fd); // raw result: already -errno
    if (*result == 0) {
        destroy_capio_ring(fd);
    }
    return true;
}

#endif // SYS_io_uring_setup
#endif // CAPIO_POSIX_UTILS_URING_HPP

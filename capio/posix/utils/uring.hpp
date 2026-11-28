#ifndef CAPIO_POSIX_UTILS_URING_HPP
#define CAPIO_POSIX_UTILS_URING_HPP

#if defined(SYS_io_uring_setup)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <linux/io_uring.h>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <unordered_map>
#include <vector>

#include "calf/SyscallLogger.h"
#include "common/syscall.hpp"
#include "utils/tid.hpp"

// Flags that older kernel headers do not define, with their values of the kernel ABI
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

/// @brief The hidden kernel ring of a CAPIO ring: it serves every SQE that is not for CAPIO
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

/**
 * @brief A ring that CAPIO serves in place of the kernel. Its memory is a memfd, the descriptor of
 * the ring: the application maps it with the mmap of the kernel, at the io_uring offsets of the SQ,
 * the CQ and the SQEs, and CAPIO maps it once, MAP_SHARED (a child of fork shares it). The file is
 * sparse.
 *
 * lock serializes the io_uring_enter and io_uring_register of the ring. The ring is freed, with
 * its kernel ring, by the last std::shared_ptr to it (see RingTable).
 */
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
    std::deque<io_uring_cqe> overflow; ///< the CQEs that wait for room in the CQ, in order
    int eventfd        = -1;           ///< registered with IORING_REGISTER_EVENTFD, or -1
    bool eventfd_async = false;        ///< registered with IORING_REGISTER_EVENTFD_ASYNC
    uint32_t waiters   = 0;            ///< threads waiting in the kernel ring
    std::mutex lock;

    ~CapioRing();
};

/// @brief @p n rounded up to a multiple of @p alignment, a power of two
inline size_t uring_align_up(size_t n, size_t alignment) {
    return (n + alignment - 1) & ~(alignment - 1);
}

/// @brief Unmap the SQ, the CQ and the SQEs of @p ring
inline void uring_unmap(CapioRing &ring) {
    if (ring.sq_ring != nullptr) {
        syscall_no_intercept(SYS_munmap, ring.sq_ring, ring.sq_ring_size);
        ring.sq_ring = nullptr;
    }
    if (ring.cq_ring != nullptr) {
        syscall_no_intercept(SYS_munmap, ring.cq_ring, ring.cq_ring_size);
        ring.cq_ring = nullptr;
    }
    if (ring.sqes != nullptr) {
        syscall_no_intercept(SYS_munmap, ring.sqes, ring.sqes_size);
        ring.sqes = nullptr;
    }
}

/**
 * @brief Lay out @p ring in @p memfd, its memory, and write its offsets to @p params, as
 * io_uring_setup does.
 * @return false if the memfd cannot be sized or mapped
 */
inline bool uring_layout(CapioRing &ring, io_uring_params *params, int memfd) {
    START_LOG(capio_tid(), "call(sq_entries=%u, cq_entries=%u, memfd=%d)", ring.sq_entries,
              ring.cq_entries, memfd);

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
    if (syscall_no_intercept(SYS_ftruncate, memfd, IORING_OFF_SQES + ring.sqes_size) < 0) {
        LOG("ftruncate of ring memfd failed");
        return false;
    }
    auto map = [memfd](size_t size, unsigned long long offset) {
        const long mem = syscall_no_intercept(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE,
                                              MAP_SHARED, memfd, offset);
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

/// @brief Set up the kernel ring of @p ring, with the sizes of @p ring. @return 0, or -errno
inline long uring_kernel_setup(CapioRing &ring) {
    START_LOG(capio_tid(), "call(sq_entries=%u, cq_entries=%u)", ring.sq_entries, ring.cq_entries);
    KernelRing &k = ring.kernel;
    io_uring_params p{};
    p.flags       = IORING_SETUP_CQSIZE;
    p.cq_entries  = ring.cq_entries;
    const long fd = syscall_no_intercept(SYS_io_uring_setup, ring.sq_entries, &p);
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
        const long mem = syscall_no_intercept(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE,
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

/// @brief Unmap and close the kernel ring @p k
inline void uring_kernel_destroy(KernelRing &k) {
    if (k.cq_ring != nullptr && k.cq_ring != k.sq_ring) {
        syscall_no_intercept(SYS_munmap, k.cq_ring, k.cq_size);
    }
    if (k.sq_ring != nullptr) {
        syscall_no_intercept(SYS_munmap, k.sq_ring, k.sq_size);
    }
    if (k.sqes != nullptr) {
        syscall_no_intercept(SYS_munmap, k.sqes, k.sqes_size);
    }
    if (k.fd >= 0) {
        syscall_no_intercept(SYS_close, k.fd);
    }
    k = KernelRing{};
}

/// The last std::shared_ptr frees the ring, so a close never unmaps it under an io_uring_enter
inline CapioRing::~CapioRing() {
    uring_unmap(*this);
    uring_kernel_destroy(kernel);
}

/// @brief The number of CQEs in the CQ of @p ring that the application has not consumed
inline uint32_t uring_cq_ready(const CapioRing &ring) {
    uint32_t head = __atomic_load_n(ring.cq_head, __ATOMIC_ACQUIRE);
    uint32_t tail = __atomic_load_n(ring.cq_tail, __ATOMIC_ACQUIRE);
    return tail - head;
}

/// @brief Whether the CQ of @p ring has room for a CQE
inline bool uring_cq_has_space(const CapioRing &ring) {
    return uring_cq_ready(ring) < *ring.cq_ring_entries;
}

/// @brief Write a CQE to the CQ of @p ring, which has room for it
inline void uring_write_cqe(CapioRing &ring, uint64_t user_data, int32_t res, uint32_t flags) {
    uint32_t tail     = *ring.cq_tail;
    io_uring_cqe &cqe = ring.cqes[tail & *ring.cq_mask];
    cqe.user_data     = user_data;
    cqe.res           = res;
    cqe.flags         = flags;
    // Release so the app sees the CQE fields before the advanced tail.
    __atomic_store_n(ring.cq_tail, tail + 1, __ATOMIC_RELEASE);
}

/// @brief Post a CQE to the CQ of @p ring. While the CQ is full, the CQEs wait in the overflow
/// list, in order, as in the kernel.
inline void uring_post_cqe(CapioRing &ring, uint64_t user_data, int32_t res, uint32_t flags = 0) {
    if (!ring.overflow.empty() || !uring_cq_has_space(ring)) {
        ring.overflow.push_back(io_uring_cqe{user_data, res, flags});
        return;
    }
    uring_write_cqe(ring, user_data, res, flags);
}

/**
 * @brief The io_uring rings of the process that CAPIO serves (see CapioRing), by the descriptor of
 * each. Implemented in src/ring_table.cpp; the process has one, ring_table().
 *
 * close runs for every descriptor, and most processes have no ring: so the table counts its rings,
 * and takes no lock while it has none. A ring is held by std::shared_ptr: when a thread closes its
 * descriptor, an io_uring_enter of another thread keeps using it until it returns, and the last of
 * them frees it.
 *
 * The descriptor of a ring is a memfd, closed by close, close_range, and dup2 or dup3 onto it: each
 * of them removes the ring from the table. A ring left in the table would take the number when the
 * kernel gives it to another file, and its close would be the one of the ring.
 */
class RingTable final {
    std::unordered_map<int, std::shared_ptr<CapioRing>> _rings;
    std::atomic<std::size_t> _count{0}; ///< size of _rings, read without the lock

    /// @brief The lock of the table, made at its first use (see FileDescriptorTable::mutex)
    static std::mutex &mutex();

  public:
    /// @brief The ring of @p fd, or nullptr if @p fd is not the descriptor of a ring
    [[nodiscard]] std::shared_ptr<CapioRing> find(int fd);

    /// @brief Add @p ring as the ring of @p fd, in place of any ring left there
    void add(int fd, std::shared_ptr<CapioRing> ring);

    /// @brief Remove the ring of @p fd. @return whether @p fd was the descriptor of a ring
    bool remove(int fd);

    /// @brief The descriptors of the rings between @p first and @p last, both included
    [[nodiscard]] std::vector<int> fdsBetween(unsigned first, unsigned last);

    /**
     * @brief Keep the table usable across fork(): its lock is held during the fork, so that the
     * child gets a whole table, and the child gets new locks, of the table and of each ring, in the
     * place of the ones the other threads of the parent may hold. Call once, before there are
     * threads.
     */
    static void protectAcrossFork();
};

/// @brief The rings of the process. Never destroyed: a thread may use it while the process exits.
RingTable &ring_table();

/**
 * @brief close of @p fd, if it is the descriptor of a ring: the memfd is closed, and the ring
 * removed from the table.
 * @return false if @p fd is not the descriptor of a ring, and nothing is done
 */
inline bool uring_close(const int fd, long *result) {
    if (ring_table().find(fd) == nullptr) {
        return false;
    }
    *result = syscall_no_intercept(SYS_close, fd); // raw result: already -errno
    if (*result == 0 || *result == -EINTR) {       // EINTR: Linux has closed it anyway
        ring_table().remove(fd);
    }
    return true;
}

#endif // SYS_io_uring_setup
#endif // CAPIO_POSIX_UTILS_URING_HPP

#ifndef CAPIO_POSIX_HANDLERS_IO_URING_HPP
#define CAPIO_POSIX_HANDLERS_IO_URING_HPP

#if defined(SYS_io_uring_setup) || defined(SYS_io_uring_enter) || defined(SYS_io_uring_register)

#include <algorithm>
#include <linux/io_uring.h>
#include <linux/openat2.h>
#include <mutex>

#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/tid.hpp"
#include "utils/uring_kernel.hpp"

#include "close.hpp"
#include "lseek.hpp"
#include "mkdir.hpp"
#include "open.hpp"
#include "read.hpp"
#include "rename.hpp"
#include "stat.hpp"
#include "statx.hpp"
#include "unlink.hpp"
#include "write.hpp"

/*
 * io_uring on a CAPIO ring: CAPIO serves the SQEs of CAPIO files, and forwards every other SQE to
 * a hidden kernel ring, whose CQEs it copies to the CQ of the application (see CapioRing).
 *
 * What CAPIO does not implement on a CAPIO file throws CapioUnimplementedException, as the system
 * calls do: an opcode or an SQE flag it does not serve, a chain that mixes CAPIO and other files,
 * the registration of a CAPIO file, and in the FS variant every SQE on a CAPIO file. A ring that
 * never touches a CAPIO file works as without CAPIO, and what the kernel itself may refuse (an
 * SQPOLL ring, the registration of the ring) is refused as the kernel does.
 */

/// @brief The setup flags that a synchronous execution satisfies: the others give EINVAL
constexpr unsigned CAPIO_URING_SETUP_FLAGS =
    IORING_SETUP_NO_SQARRAY | IORING_SETUP_CQSIZE | IORING_SETUP_CLAMP | IORING_SETUP_SUBMIT_ALL |
    IORING_SETUP_COOP_TASKRUN | IORING_SETUP_TASKRUN_FLAG | IORING_SETUP_SINGLE_ISSUER |
    IORING_SETUP_DEFER_TASKRUN;

/// @brief The SQE flags that CAPIO honours on the SQEs it serves: see uring_capio_exec
constexpr uint8_t CAPIO_URING_SQE_FLAGS =
    IOSQE_IO_DRAIN | IOSQE_IO_LINK | IOSQE_IO_HARDLINK | IOSQE_ASYNC | IOSQE_CQE_SKIP_SUCCESS;

/// @brief The features of the kernel ring that a CAPIO ring has too
constexpr uint32_t CAPIO_URING_KERNEL_FEATURES = IORING_FEAT_CUR_PERSONALITY |
                                                 IORING_FEAT_FAST_POLL | IORING_FEAT_POLL_32BITS |
                                                 IORING_FEAT_EXT_ARG
#ifdef IORING_FEAT_NATIVE_WORKERS
                                                 | IORING_FEAT_NATIVE_WORKERS
#endif
#ifdef IORING_FEAT_RSRC_TAGS
                                                 | IORING_FEAT_RSRC_TAGS
#endif
#ifdef IORING_FEAT_LINKED_FILE
                                                 | IORING_FEAT_LINKED_FILE
#endif
    ;

/// @brief The limits of the kernel: IORING_MAX_ENTRIES and IORING_MAX_CQ_ENTRIES
constexpr uint32_t CAPIO_URING_MAX_SQ_ENTRIES = 32768;
constexpr uint32_t CAPIO_URING_MAX_CQ_ENTRIES = 2 * CAPIO_URING_MAX_SQ_ENTRIES;

/// @brief IORING_REGISTER_USE_REGISTERED_RING, which older kernel headers do not define
constexpr unsigned CAPIO_URING_REGISTER_USE_REGISTERED_RING = 1U << 31;

/**
 * @brief The number of entries of a ring asked for @p n: @p n rounded up to a power of two, as the
 * kernel does, and at most @p max with @p clamp (IORING_SETUP_CLAMP).
 * @return false if @p n is 0, or above @p max without @p clamp: EINVAL
 */
inline bool uring_entries(uint32_t n, uint32_t max, bool clamp, uint32_t &out) {
    if (n == 0 || (n > max && !clamp)) {
        return false;
    }
    n          = std::min(n, max);
    uint32_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    out = p;
    return true;
}

/// @brief The sizes of the SQ and of the CQ of @p ring, from io_uring_setup's @p entries and
/// @p params. @return false if they are invalid: EINVAL
inline bool uring_ring_sizes(uint32_t entries, const io_uring_params *params, CapioRing &ring) {
    const bool clamp = params->flags & IORING_SETUP_CLAMP;
    if (!uring_entries(entries, CAPIO_URING_MAX_SQ_ENTRIES, clamp, ring.sq_entries)) {
        return false;
    }
    if (!(params->flags & IORING_SETUP_CQSIZE)) {
        ring.cq_entries = ring.sq_entries * 2;
        return true;
    }
    return uring_entries(params->cq_entries, CAPIO_URING_MAX_CQ_ENTRIES, clamp, ring.cq_entries) &&
           ring.cq_entries >= ring.sq_entries;
}

/**
 * @brief io_uring_setup: a CAPIO ring, with its hidden kernel ring.
 * @return the descriptor of the ring, or CAPIO_POSIX_SYSCALL_ERRNO: EINVAL for flags CAPIO does
 * not support, the error of the kernel if it does not support io_uring
 */
inline int capio_io_uring_setup(unsigned entries, io_uring_params *params, long tid) {
    START_LOG(tid, "call(entries=%u, params=%p)", entries, static_cast<void *>(params));

    const unsigned flags = params == nullptr ? 0 : params->flags;
    const bool bad_combination =
        ((flags & IORING_SETUP_DEFER_TASKRUN) && !(flags & IORING_SETUP_SINGLE_ISSUER)) ||
        ((flags & IORING_SETUP_TASKRUN_FLAG) &&
         !(flags & (IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN)));
    if (params == nullptr || entries == 0 || (flags & ~CAPIO_URING_SETUP_FLAGS) ||
        bad_combination) {
        LOG("rejecting setup: entries=%u flags=0x%x", entries, flags);
        errno = EINVAL;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    // The ring memory lives in a memfd (see CapioRing).
    const long memfd =
        capio_kernel_result(syscall_no_intercept(SYS_memfd_create, "capio_uring", MFD_CLOEXEC));
    if (memfd < 0) {
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    const int ring_fd = static_cast<int>(memfd);

    auto ring        = std::make_shared<CapioRing>();
    ring->no_sqarray = flags & IORING_SETUP_NO_SQARRAY;
    long res         = uring_ring_sizes(entries, params, *ring) ? 0 : -EINVAL;
    if (res == 0) {
        params->sq_entries = ring->sq_entries;
        params->cq_entries = ring->cq_entries;
        res = uring_layout(*ring, params, ring_fd) ? uring_kernel_setup(*ring) : -ENOMEM;
    }
    if (capio_kernel_result(res) < 0) {
        syscall_no_intercept(SYS_close, ring_fd);
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    // No SINGLE_MMAP: the SQ and CQ are separate memfd regions.
    params->features = IORING_FEAT_SUBMIT_STABLE | IORING_FEAT_RW_CUR_POS | IORING_FEAT_CQE_SKIP |
                       (ring->kernel.features & CAPIO_URING_KERNEL_FEATURES);

    LOG("io_uring_setup emulated: ring_fd=%d sq_entries=%u cq_entries=%u", ring_fd,
        ring->sq_entries, ring->cq_entries);
    ring_table().add(ring_fd, std::move(ring));
    return ring_fd;
}

/// @brief The res of a CQE from what a capio_* function returns: -errno for an error
inline int32_t uring_result(off64_t res) {
    return static_cast<int32_t>(res == CAPIO_POSIX_SYSCALL_ERRNO ? -errno : res);
}

/**
 * @brief A read or a write of a CAPIO file. capio_readv and capio_writev use the offset of the
 * descriptor: an SQE with an offset seeks there (which flushes the caches and tells the server),
 * then back. An offset of -1 is the offset of the descriptor, as in the kernel.
 */
inline int32_t uring_capio_rw(const io_uring_sqe *sqe, const iovec *iov, int iovcnt, bool is_write,
                              long tid) {
    START_LOG(tid, "call(fd=%d, off=%lld, iovcnt=%d, is_write=%d)", sqe->fd,
              static_cast<long long>(sqe->off), iovcnt, is_write);
    auto transfer = [&]() {
        return uring_result(is_write ? capio_writev(sqe->fd, iov, iovcnt, tid)
                                     : capio_readv(sqe->fd, iov, iovcnt, tid));
    };
    if (sqe->off == static_cast<uint64_t>(-1)) {
        return transfer();
    }

    off64_t saved = fd_table->offset(sqe->fd);
    if (capio_lseek(sqe->fd, static_cast<off64_t>(sqe->off), SEEK_SET, tid) < 0) {
        return -errno;
    }
    int32_t res = transfer();
    if (capio_lseek(sqe->fd, saved, SEEK_SET, tid) < 0) {
        LOG("could not restore offset %ld on fd %d", saved, sqe->fd);
    }
    return res;
}

/// @brief IORING_OP_READ, _WRITE, _READV and _WRITEV (@p vectored) of a CAPIO file
inline int32_t uring_rw(const io_uring_sqe *sqe, bool vectored, bool is_write, long tid) {
    iovec single{reinterpret_cast<void *>(sqe->addr), sqe->len};
    const auto *iov  = vectored ? reinterpret_cast<const iovec *>(sqe->addr) : &single;
    const int iovcnt = vectored ? static_cast<int>(sqe->len) : 1;
    return uring_capio_rw(sqe, iov, iovcnt, is_write, tid);
}

/// @brief Whether the path at @p pathname, relative to @p dirfd, is in CAPIO_DIR. Only a guess of
/// where the SQE goes: the capio_* function decides, and the SQEs it skips go to the kernel.
inline bool uring_is_capio_at(int dirfd, uint64_t pathname) {
    if (pathname == 0) {
        return false;
    }
    std::filesystem::path path(reinterpret_cast<const char *>(pathname));
    if (path.is_relative()) {
        if (dirfd == AT_FDCWD) {
            path = capio_posix_realpath(path);
        } else {
            const std::filesystem::path dir = get_dir_path(dirfd);
            path = dir.empty() ? std::filesystem::path() : (dir / path).lexically_normal();
        }
    }
    return !path.empty() && is_capio_path(path);
}

/// @brief Whether CAPIO serves @p sqe: an SQE on a CAPIO file, or one of the operations on paths
/// that CAPIO handles as system calls. Every other SQE, of any opcode, goes to the kernel ring.
inline bool uring_for_capio(const io_uring_sqe *sqe) {
    if (sqe->flags & IOSQE_FIXED_FILE) {
        return false;
    }
    switch (sqe->opcode) {
    case IORING_OP_OPENAT:
    case IORING_OP_OPENAT2:
    case IORING_OP_STATX:
    case IORING_OP_UNLINKAT:
    case IORING_OP_MKDIRAT:
        return fd_table->contains(sqe->fd) || uring_is_capio_at(sqe->fd, sqe->addr);
    case IORING_OP_RENAMEAT:
        return uring_is_capio_at(sqe->fd, sqe->addr) ||
               uring_is_capio_at(static_cast<int>(sqe->len), sqe->addr2);
    case IORING_OP_SPLICE:
    case IORING_OP_TEE:
        return fd_table->contains(sqe->fd) || fd_table->contains(sqe->splice_fd_in);
    default:
        return sqe->fd >= 0 && fd_table->contains(sqe->fd);
    }
}

/**
 * @brief Serve @p sqe, an SQE for CAPIO, as the system call it stands for. An opcode or a flag
 * that CAPIO does not serve, and every SQE in the FS variant, throw CapioUnimplementedException.
 * @param res the res of its CQE
 * @return false if CAPIO leaves it to the kernel ring: its capio_* function skipped it
 */
inline bool uring_capio_exec(const io_uring_sqe *sqe, long tid, int32_t &res) {
    START_LOG(tid, "call(opcode=%u, fd=%d, user_data=%llu)", sqe->opcode, sqe->fd,
              static_cast<unsigned long long>(sqe->user_data));

    FS_MODE({ throw CapioUnimplementedException("io_uring on a CAPIO file"); })
    if (sqe->flags & ~CAPIO_URING_SQE_FLAGS) {
        throw CapioUnimplementedException("io_uring with SQE flags " + std::to_string(sqe->flags) +
                                          " on a CAPIO file");
    }
    const std::string_view path(sqe->addr != 0 ? reinterpret_cast<const char *>(sqe->addr) : "");
    off64_t r    = CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    long handled = 0;
    switch (sqe->opcode) {
    case IORING_OP_FSYNC:
        res = 0; // CAPIO commit rules give durability
        return true;
    case IORING_OP_READ:
    case IORING_OP_READ_FIXED:
        res = uring_rw(sqe, false, false, tid);
        return true;
    case IORING_OP_WRITE:
    case IORING_OP_WRITE_FIXED:
        res = uring_rw(sqe, false, true, tid);
        return true;
    case IORING_OP_READV:
        res = uring_rw(sqe, true, false, tid);
        return true;
    case IORING_OP_WRITEV:
        res = uring_rw(sqe, true, true, tid);
        return true;
    case IORING_OP_OPENAT:
        r = capio_openat(sqe->fd, path, static_cast<int>(sqe->open_flags),
                         static_cast<mode_t>(sqe->len), tid);
        break;
    case IORING_OP_OPENAT2: {
        const auto *how = reinterpret_cast<const open_how *>(sqe->addr2);
        r               = capio_openat(sqe->fd, path, static_cast<int>(how->flags),
                                       static_cast<mode_t>(how->mode), tid);
        break;
    }
    case IORING_OP_STATX:
        r = capio_statx(sqe->fd, path, static_cast<int>(sqe->statx_flags),
                        static_cast<int>(sqe->len), reinterpret_cast<struct statx *>(sqe->addr2),
                        tid);
        break;
    case IORING_OP_UNLINKAT:
        r = capio_unlinkat(sqe->fd, path, static_cast<int>(sqe->unlink_flags), tid);
        break;
    case IORING_OP_MKDIRAT:
        r = capio_mkdirat(sqe->fd, path, static_cast<mode_t>(sqe->len), tid);
        break;
    // close and rename have no capio_* function: their handlers hold the logic.
    case IORING_OP_CLOSE:
        if (close_handler(sqe->fd, 0, 0, 0, 0, 0, &handled) == CAPIO_POSIX_SYSCALL_SKIP) {
            return false;
        }
        res = static_cast<int32_t>(handled);
        return true;
    case IORING_OP_RENAMEAT:
        if (renameat2_handler(sqe->fd, static_cast<long>(sqe->addr), sqe->len,
                              static_cast<long>(sqe->addr2), sqe->rename_flags, 0,
                              &handled) == CAPIO_POSIX_SYSCALL_SKIP) {
            return false;
        }
        res = static_cast<int32_t>(handled);
        return true;
    default:
        throw CapioUnimplementedException("io_uring opcode " + std::to_string(sqe->opcode) +
                                          " on a CAPIO file");
    }
    if (r == CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        return false;
    }
    res = uring_result(r);
    return true;
}

/// @brief Whether a linked SQE with result @p res fails its chain: an error, or a short read or
/// write, as in the kernel
inline bool uring_breaks_link(const io_uring_sqe *sqe, int32_t res) {
    if (res < 0) {
        return true;
    }
    if (sqe->opcode == IORING_OP_READ || sqe->opcode == IORING_OP_WRITE) {
        return static_cast<uint32_t>(res) < sqe->len;
    }
    if (sqe->opcode == IORING_OP_READV || sqe->opcode == IORING_OP_WRITEV) {
        const auto *iov = reinterpret_cast<const iovec *>(sqe->addr);
        size_t total    = 0;
        for (uint32_t i = 0; i < sqe->len; ++i) {
            total += iov[i].iov_len;
        }
        return static_cast<size_t>(res) < total;
    }
    return false;
}

/// @brief Signal the eventfd registered on @p ring for the CQEs that CAPIO posted: the kernel ring
/// signals it for its own
inline void uring_signal_eventfd(const CapioRing &ring) {
    if (ring.eventfd >= 0 && !ring.eventfd_async) {
        const uint64_t one = 1;
        syscall_no_intercept(SYS_write, ring.eventfd, &one, sizeof(one));
    }
}

/// @brief The SQE at position @p pos of the SQ, or nullptr if its index is out of the ring
inline const io_uring_sqe *uring_sqe_at(const CapioRing &ring, uint32_t pos) {
    const uint32_t slot = pos & *ring.sq_mask;
    const uint32_t idx  = ring.no_sqarray ? slot : ring.sq_array[slot];
    return idx < ring.sq_entries ? &ring.sqes[idx] : nullptr;
}

/**
 * @brief Run the @p count linked SQEs at @p head, a chain with at least one SQE for CAPIO. A chain
 * that mixes CAPIO and kernel SQEs is not implemented (CapioUnimplementedException). A single SQE
 * that CAPIO skips goes to the kernel ring.
 * @return whether a CQE was posted
 */
inline bool uring_run_chain(CapioRing &ring, uint32_t head, uint32_t count, long tid) {
    bool chain_failed = false;
    bool posted       = false;
    for (uint32_t i = 0; i < count; ++i) {
        const io_uring_sqe sqe = *uring_sqe_at(ring, head + i); // the app may reuse the slot
        int32_t res            = 0;
        if (chain_failed || sqe.opcode == IORING_OP_LINK_TIMEOUT) {
            res = -ECANCELED;
        } else if (sqe.opcode == IORING_OP_NOP) {
            res = 0; // no side effect: completed here, in chain order
        } else if (!uring_for_capio(&sqe) || !uring_capio_exec(&sqe, tid, res)) {
            if (count > 1) {
                throw CapioUnimplementedException(
                    "an io_uring chain of CAPIO files and other files");
            }
            uring_kernel_queue(ring, sqe);
            continue;
        }
        if (!chain_failed && (sqe.flags & IOSQE_IO_LINK) && uring_breaks_link(&sqe, res)) {
            chain_failed = true;
        }
        if (!((sqe.flags & IOSQE_CQE_SKIP_SUCCESS) && res >= 0)) {
            uring_post_cqe(ring, sqe.user_data, res);
            posted = true;
        }
    }
    return posted;
}

/**
 * @brief Submit up to @p to_submit SQEs of the SQ. A chain is one unit: the kernel ring takes it if
 * none of its SQEs is for CAPIO.
 * @param posted set if CAPIO posted a CQE
 * @return the number of SQEs submitted
 */
inline uint32_t uring_submit(CapioRing &ring, unsigned to_submit, long tid, bool &posted,
                             std::unique_lock<std::mutex> &lock) {
    uint32_t head      = *ring.sq_head;
    uint32_t tail      = __atomic_load_n(ring.sq_tail, __ATOMIC_ACQUIRE);
    uint32_t wanted    = std::min<uint32_t>(to_submit, tail - head);
    uint32_t submitted = 0;
    while (submitted < wanted) {
        const io_uring_sqe *sqe = uring_sqe_at(ring, head);
        if (sqe == nullptr) {
            ++*ring.sq_dropped;
            ++head;
            break;
        }
        uint32_t count = 1;
        bool for_capio = uring_for_capio(sqe);
        for (const io_uring_sqe *last = sqe;
             (last->flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK)) && submitted + count < wanted;
             ++count) {
            last = uring_sqe_at(ring, head + count);
            if (last == nullptr) {
                break;
            }
            for_capio = for_capio || uring_for_capio(last);
        }
        if (for_capio && !uring_cq_has_space(ring)) {
            break;
        }
        if (sqe->flags & IOSQE_IO_DRAIN) {
            uring_drain(ring, lock);
        }
        if (for_capio) {
            uring_kernel_flush(ring);
            posted = uring_run_chain(ring, head, count, tid) || posted;
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                uring_kernel_queue(ring, *uring_sqe_at(ring, head + i));
            }
        }
        head += count;
        submitted += count;
    }
    __atomic_store_n(ring.sq_head, head, __ATOMIC_RELEASE);
    uring_kernel_flush(ring);
    return submitted;
}

/**
 * @brief io_uring_enter of a CAPIO ring: submit, then wait for @p min_complete CQEs with
 * IORING_ENTER_GETEVENTS.
 * @return the number of SQEs submitted, CAPIO_POSIX_SYSCALL_REQUEST_SKIP if @p ring_fd is not a
 * CAPIO ring, or CAPIO_POSIX_SYSCALL_ERRNO
 */
inline off64_t capio_io_uring_enter(int ring_fd, unsigned to_submit, unsigned min_complete,
                                    unsigned flags, const UringWaitArg &wait, long tid) {
    START_LOG(tid, "call(ring_fd=%d, to_submit=%u, min_complete=%u, flags=0x%x)", ring_fd,
              to_submit, min_complete, flags);

    const std::shared_ptr<CapioRing> ring = ring_table().find(ring_fd);
    if (ring == nullptr) {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
    if (flags & ~(IORING_ENTER_GETEVENTS | IORING_ENTER_SQ_WAKEUP | IORING_ENTER_SQ_WAIT |
                  IORING_ENTER_EXT_ARG)) {
        errno = EINVAL;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    std::unique_lock<std::mutex> lock(ring->lock);

    const uint32_t queued    = __atomic_load_n(ring->sq_tail, __ATOMIC_ACQUIRE) - *ring->sq_head;
    bool posted              = false;
    const uint32_t submitted = uring_submit(*ring, to_submit, tid, posted, lock);
    if (posted) {
        uring_signal_eventfd(*ring);
        uring_wake_waiters(*ring);
    }
    if (to_submit > 0 && queued > 0 && submitted == 0 && !uring_cq_has_space(*ring)) {
        errno = EBUSY;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    long waited = 0;
    if (flags & IORING_ENTER_GETEVENTS) {
        waited = uring_wait(*ring, min_complete, wait, lock);
    } else {
        uring_collect(*ring);
    }
    LOG("io_uring_enter: %u SQEs submitted, wait result %ld", submitted, waited);
    if (waited < 0 && submitted == 0) {
        errno = static_cast<int>(-waited);
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    return submitted;
}

/// @brief Whether one of the @p count descriptors at @p fds is a CAPIO file: the kernel ring would
/// use the placeholder descriptor behind it
inline bool uring_has_capio_fd(uint64_t fds, uint32_t count) {
    const auto *fd = reinterpret_cast<const int *>(fds);
    for (uint32_t i = 0; fd != nullptr && i < count; ++i) {
        if (fd[i] >= 0 && fd_table->contains(fd[i])) {
            return true;
        }
    }
    return false;
}

/**
 * @brief io_uring_register of a CAPIO ring: forwarded to the kernel ring. CAPIO files cannot be
 * registered (CapioUnimplementedException), and neither can the ring itself (EOPNOTSUPP).
 * @return what the kernel returns, CAPIO_POSIX_SYSCALL_REQUEST_SKIP if @p ring_fd is not a CAPIO
 * ring, or CAPIO_POSIX_SYSCALL_ERRNO: EOPNOTSUPP for the registration of the ring
 */
inline off64_t capio_io_uring_register(int ring_fd, unsigned opcode, void *arg, unsigned count,
                                       long tid) {
    START_LOG(tid, "call(ring_fd=%d, opcode=%u, nr_args=%u)", ring_fd, opcode, count);

    const std::shared_ptr<CapioRing> ring = ring_table().find(ring_fd);
    if (ring == nullptr) {
        return CAPIO_POSIX_SYSCALL_REQUEST_SKIP;
    }
    const std::lock_guard<std::mutex> lock(ring->lock);

    bool refused     = opcode & CAPIO_URING_REGISTER_USE_REGISTERED_RING;
    bool capio_files = false;
    switch (opcode) {
    case IORING_REGISTER_FILES:
        capio_files = uring_has_capio_fd(reinterpret_cast<uint64_t>(arg), count);
        break;
    case IORING_REGISTER_FILES2: {
        const auto *reg = static_cast<const io_uring_rsrc_register *>(arg);
        capio_files     = reg != nullptr && uring_has_capio_fd(reg->data, reg->nr);
        break;
    }
    case IORING_REGISTER_FILES_UPDATE: {
        const auto *update = static_cast<const io_uring_files_update *>(arg);
        capio_files        = update != nullptr && uring_has_capio_fd(update->fds, count);
        break;
    }
    case IORING_REGISTER_FILES_UPDATE2: {
        const auto *update = static_cast<const io_uring_rsrc_update2 *>(arg);
        capio_files        = update != nullptr && uring_has_capio_fd(update->data, update->nr);
        break;
    }
    case IORING_REGISTER_RING_FDS: // enter must receive CAPIO's ring fd
    case IORING_UNREGISTER_RING_FDS:
        refused = true;
        break;
    default:
        break;
    }
    if (capio_files) {
        throw CapioUnimplementedException("io_uring_register of a CAPIO file");
    }
    if (refused) {
        LOG("register opcode %u refused", opcode);
        errno = EOPNOTSUPP;
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }

    const long r = capio_kernel_result(
        syscall_no_intercept(SYS_io_uring_register, ring->kernel.fd, opcode, arg, count));
    if (r < 0) {
        return CAPIO_POSIX_SYSCALL_ERRNO;
    }
    if (opcode == IORING_REGISTER_EVENTFD || opcode == IORING_REGISTER_EVENTFD_ASYNC) {
        ring->eventfd       = *static_cast<const int *>(arg);
        ring->eventfd_async = opcode == IORING_REGISTER_EVENTFD_ASYNC;
    } else if (opcode == IORING_UNREGISTER_EVENTFD) {
        ring->eventfd = -1;
    }
    return r;
}

#ifdef SYS_io_uring_setup
int io_uring_setup_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                           long *result) {
    const long tid = capio_tid();
    return posix_return_value(capio_io_uring_setup(static_cast<unsigned>(arg0),
                                                   reinterpret_cast<io_uring_params *>(arg1), tid),
                              result);
}
#endif // SYS_io_uring_setup

#ifdef SYS_io_uring_enter
int io_uring_enter_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                           long *result) {
    const long tid = capio_tid();
    const UringWaitArg wait{static_cast<unsigned>(arg3) & IORING_ENTER_EXT_ARG,
                            reinterpret_cast<const void *>(arg4), static_cast<size_t>(arg5)};
    return posix_return_value(
        capio_io_uring_enter(static_cast<int>(arg0), static_cast<unsigned>(arg1),
                             static_cast<unsigned>(arg2), static_cast<unsigned>(arg3), wait, tid),
        result);
}
#endif // SYS_io_uring_enter

#ifdef SYS_io_uring_register
int io_uring_register_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                              long *result) {
    const long tid = capio_tid();
    return posix_return_value(
        capio_io_uring_register(static_cast<int>(arg0), static_cast<unsigned>(arg1),
                                reinterpret_cast<void *>(arg2), static_cast<unsigned>(arg3), tid),
        result);
}
#endif // SYS_io_uring_register

#endif // SYS_io_uring_setup || SYS_io_uring_enter || SYS_io_uring_register
#endif // CAPIO_POSIX_HANDLERS_IO_URING_HPP

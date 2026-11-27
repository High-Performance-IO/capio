#ifndef CAPIO_POSIX_HANDLERS_FCNTL_HPP
#define CAPIO_POSIX_HANDLERS_FCNTL_HPP

#if defined(SYS_fcntl)

#include <fcntl.h>
#include <string>
#include <unordered_map>

#include "utils/common.hpp"
#include "utils/requests.hpp"
#include "utils/tid.hpp"

/**
 * The file status flags F_SETFL may change. The access mode and the creation flags stay as open()
 * set them, as in the kernel.
 */
constexpr int CAPIO_FCNTL_SETFL_MASK = O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK;

/**
 * Read/write hints set with F_SET_RW_HINT. The kernel keeps them on the inode, so they are kept
 * per CAPIO path and every descriptor of the file sees them.
 */
inline std::unordered_map<std::string, uint64_t> &capio_rw_hints() {
    static std::unordered_map<std::string, uint64_t> hints;
    return hints;
}

/**
 * Duplicate a CAPIO descriptor onto the lowest free descriptor not below @min_fd. A CAPIO
 * descriptor is a real descriptor (of /dev/null in the memory variant), so the kernel picks the
 * number.
 */
inline long capio_fcntl_dupfd(long tid, int fd, int min_fd, bool is_cloexec) {
    START_LOG(tid, "call(fd=%d, min_fd=%d, is_cloexec=%d)", fd, min_fd, is_cloexec);
    const long res =
        syscall_no_intercept(SYS_fcntl, fd, is_cloexec ? F_DUPFD_CLOEXEC : F_DUPFD, min_fd);
    if (res < 0) {
        return res;
    }
    fd_table->dup(fd, static_cast<int>(res), is_cloexec);
    dup_request(fd, static_cast<int>(res), tid);
    return res;
}

/**
 * Advisory locks. CAPIO files have no kernel inode the processes share, so locks are not enforced
 * between processes: a lock is always granted and F_GETLK always reports the range as unlocked.
 * The arguments are checked as the kernel checks them.
 */
inline long capio_fcntl_lock(int fd, int cmd, struct flock *lock) {
    if (lock == nullptr) {
        return -EFAULT;
    }
    bool get = cmd == F_GETLK;
#ifdef F_OFD_GETLK
    get            = get || cmd == F_OFD_GETLK;
    const bool ofd = cmd == F_OFD_GETLK || cmd == F_OFD_SETLK || cmd == F_OFD_SETLKW;
    if (ofd && lock->l_pid != 0) {
        return -EINVAL;
    }
#endif
    const int access = fd_table->flags(fd) & O_ACCMODE;
    switch (lock->l_type) {
    case F_RDLCK:
        if (!get && access == O_WRONLY) {
            return -EBADF;
        }
        break;
    case F_WRLCK:
        if (!get && access == O_RDONLY) {
            return -EBADF;
        }
        break;
    case F_UNLCK:
        if (get) {
            return -EINVAL;
        }
        break;
    default:
        return -EINVAL;
    }
    if (get) {
        lock->l_type = F_UNLCK;
    }
    return 0;
}

int fcntl_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result) {
    auto fd  = static_cast<int>(arg0);
    auto cmd = static_cast<int>(arg1);

    START_LOG(capio_tid(), "call(fd=%d, cmd=%d, arg=%ld)", fd, cmd, arg2);

    if (!fd_table->contains(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }

    FS_MODE({
        // A real descriptor: the kernel runs it, and CAPIO tracks the copies and close-on-exec
        if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) {
            *result =
                capio_fcntl_dupfd(capio_tid(), fd, static_cast<int>(arg2), cmd == F_DUPFD_CLOEXEC);
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        if (cmd == F_SETFD) {
            *result = syscall_no_intercept(SYS_fcntl, fd, cmd, arg2);
            if (*result == 0) {
                fd_table->setCloexec(fd, (arg2 & FD_CLOEXEC) == FD_CLOEXEC);
            }
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        return CAPIO_POSIX_SYSCALL_SKIP;
    })

    switch (cmd) {
    case F_DUPFD:
        *result = capio_fcntl_dupfd(capio_tid(), fd, static_cast<int>(arg2), false);
        break;

    case F_DUPFD_CLOEXEC:
        *result = capio_fcntl_dupfd(capio_tid(), fd, static_cast<int>(arg2), true);
        break;

    case F_GETFD:
        *result = fd_table->cloexec(fd) ? FD_CLOEXEC : 0;
        break;

    case F_SETFD:
        fd_table->setCloexec(fd, (arg2 & FD_CLOEXEC) == FD_CLOEXEC);
        *result = 0;
        break;

    case F_GETFL:
        *result = fd_table->flags(fd);
        break;

    case F_SETFL:
        fd_table->setFlags(fd, (fd_table->flags(fd) & ~CAPIO_FCNTL_SETFL_MASK) |
                                   (static_cast<int>(arg2) & CAPIO_FCNTL_SETFL_MASK));
        *result = 0;
        break;

    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
#ifdef F_OFD_GETLK
    case F_OFD_GETLK:
    case F_OFD_SETLK:
    case F_OFD_SETLKW:
#endif
        *result = capio_fcntl_lock(fd, cmd, reinterpret_cast<struct flock *>(arg2));
        break;

    // Signal-driven I/O: the owner and the signal are kept on the descriptor of /dev/null, which
    // stores them as for any file. No signal is ever sent for a CAPIO file.
    case F_GETOWN:
    case F_SETOWN:
    case F_GETSIG:
    case F_SETSIG:
#ifdef F_GETOWN_EX
    case F_GETOWN_EX:
    case F_SETOWN_EX:
#endif
        *result = syscall_no_intercept(SYS_fcntl, fd, cmd, arg2);
        break;

    // A lease needs the kernel to tell its holder when another process opens the file, which
    // CAPIO cannot do: no lease is held, and asking for one fails as if the file were open
    // elsewhere.
    case F_GETLEASE:
        *result = F_UNLCK;
        break;

    case F_SETLEASE:
        if (arg2 == F_UNLCK) {
            *result = 0;
        } else if (arg2 == F_RDLCK || arg2 == F_WRLCK) {
            *result = -EAGAIN;
        } else {
            *result = -EINVAL;
        }
        break;

    // Directory notifications: CAPIO sends none, so only removing them succeeds.
    case F_NOTIFY:
        if ((fd_table->flags(fd) & O_DIRECTORY) != O_DIRECTORY) {
            *result = -ENOTDIR;
        } else {
            *result = arg2 == 0 ? 0 : -EINVAL;
        }
        break;

#ifdef F_DUPFD_QUERY
    // Whether @arg2 refers to the same open file description: dup'd CAPIO descriptors share their
    // offset.
    case F_DUPFD_QUERY: {
        const auto other = static_cast<int>(arg2);
        if (fd_table->contains(other)) {
            *result = fd_table->sharesOffset(fd, other) ? 1 : 0;
        } else if (syscall_no_intercept(SYS_fcntl, other, F_GETFD) < 0) {
            *result = -EBADF;
        } else {
            *result = 0;
        }
        break;
    }
#endif

    // A CAPIO file is not a pipe: the kernel answers EBADF for a regular file.
    case F_GETPIPE_SZ:
    case F_SETPIPE_SZ:
        *result = -EBADF;
        break;

#ifdef F_ADD_SEALS
    // Seals exist only on memfd and shmem files.
    case F_ADD_SEALS:
    case F_GET_SEALS:
        *result = -EINVAL;
        break;
#endif

#ifdef F_GET_RW_HINT
    case F_GET_RW_HINT:
    case F_SET_RW_HINT: {
        auto *hint = reinterpret_cast<uint64_t *>(arg2);
        if (hint == nullptr) {
            *result = -EFAULT;
            break;
        }
        const std::string &path = fd_table->path(fd);
        if (cmd == F_GET_RW_HINT) {
            const auto it = capio_rw_hints().find(path);
            *hint         = it == capio_rw_hints().end() ? RWH_WRITE_LIFE_NOT_SET : it->second;
            *result       = 0;
        } else if (*hint > RWH_WRITE_LIFE_EXTREME) {
            *result = -EINVAL;
        } else {
            capio_rw_hints()[path] = *hint;
            *result                = 0;
        }
        break;
    }
#endif

    // Anything else, F_CREATED_QUERY among them, is answered as a kernel that does not know it.
    default:
        LOG("fcntl with cmd %d is not supported on CAPIO files", cmd);
        *result = -EINVAL;
    }
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

#endif // SYS_fcntl
#endif // CAPIO_POSIX_HANDLERS_FCNTL_HPP

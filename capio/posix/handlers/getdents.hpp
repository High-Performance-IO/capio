#ifndef CAPIO_POSIX_HANDLERS_GETDENTS_HPP
#define CAPIO_POSIX_HANDLERS_GETDENTS_HPP

#if defined(SYS_getdents) || defined(SYS_getdents64)

#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#include "common/dirent.hpp"

#include "utils/common.hpp"
#include "utils/data.hpp"
#include "utils/requests.hpp"

/*
 * Listing directories in the FS variant.
 *
 * Only directories with an on_n_files rule make a consumer wait; every other directory is listed
 * by the kernel as usual. The wait can't be left to the kernel: once a directory descriptor has
 * reached the end, it never sees entries created afterwards. So when a listing runs out, CAPIO
 * reads the directory again through a fresh descriptor, returns only the names it hasn't returned
 * yet, and if there are none, asks the server to wait for a new entry or for the commit.
 *
 * - update mode: wait for the commit, then return every entry.
 * - no_update mode: return entries as they appear, and end at the commit.
 *
 * This is done in getdents, below readdir, so that scandir, glob and nftw (which call readdir from
 * inside glibc) wait too. The position of an entry (d_off) is its index in the listing.
 */
struct CapioListingFs {
    struct Entry {
        ino64_t inode;
        unsigned char type;
        std::string name;
    };

    bool is_started = false;
    bool as_is      = false; ///< No on_n_files rule: the kernel lists this directory
    bool is_final   = false; ///< Read again after the commit: no entry can be missing
    std::vector<Entry> entries;
    std::unordered_set<std::string> names;
    size_t position = 0;
};

/**
 * @brief The listings in progress, by descriptor number, kept until the descriptor is closed.
 *
 * The map has a lock because threads share descriptors. A listing is used without holding the
 * lock, since getdents may wait for the server; the shared_ptr keeps it alive if another thread
 * closes the descriptor meanwhile.
 */
class CapioListingsFs {
    std::mutex _mutex;
    std::unordered_map<int, std::shared_ptr<CapioListingFs>> _listings;

  public:
    /// @brief The listing of @p fd, creating an empty one if there is none
    std::shared_ptr<CapioListingFs> get(const int fd) {
        const std::lock_guard lock(_mutex);
        auto &listing = _listings[fd];
        if (listing == nullptr) {
            listing = std::make_shared<CapioListingFs>();
        }
        return listing;
    }

    /// @brief The listing of @p fd, or nullptr if there is none
    std::shared_ptr<CapioListingFs> find(const int fd) {
        const std::lock_guard lock(_mutex);
        const auto found = _listings.find(fd);
        return found == _listings.end() ? nullptr : found->second;
    }

    /// @brief Drop the listing of @p fd, when that descriptor number is closed or reused
    void forget(const int fd) {
        const std::lock_guard lock(_mutex);
        _listings.erase(fd);
    }
};

inline CapioListingsFs &capio_listings_fs() {
    static auto *listings = new CapioListingsFs();
    return *listings;
}

inline void capio_forget_listing_fs(const int fd) { capio_listings_fs().forget(fd); }

/**
 * Read the directory of @p fd again, and add to @p listing the entries it doesn't have yet.
 *
 * @return how many entries the directory has, not counting "." and "..", or -errno
 */
inline long capio_rescan_fs(const int fd, CapioListingFs &listing) {
    const long scan =
        syscall_no_intercept(SYS_openat, fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (scan < 0) {
        return scan;
    }
    long n_entries = 0, length;
    alignas(8) char buffer[8192];
    while ((length = syscall_no_intercept(SYS_getdents64, scan, buffer, sizeof(buffer))) > 0) {
        for (long at = 0; at < length;) {
            const auto *entry = reinterpret_cast<const linux_dirent64 *>(buffer + at);
            at += entry->d_reclen;
            const std::string_view name(entry->d_name);
            if (is_forbidden_path(name)) {
                continue; // a CAPIO-CL token file, not a file of the workflow
            }
            n_entries += name != "." && name != "..";
            if (listing.names.emplace(name).second) {
                listing.entries.push_back({entry->d_ino, entry->d_type, std::string(name)});
            }
        }
    }
    syscall_no_intercept(SYS_close, scan);
    return length < 0 ? length : n_entries;
}

/// @brief Copy the next entries of @p listing into @p buffer, in the format of getdents or
/// getdents64
inline long capio_serve_entries_fs(CapioListingFs &listing, char *buffer, const size_t count,
                                   const bool is64bit) {
    size_t used = 0;
    while (listing.position < listing.entries.size()) {
        const auto &entry    = listing.entries[listing.position];
        // Record layout. getdents64: ino, off, reclen, type, name. getdents: ino, off, reclen,
        // name, padding, and the type in the record's last byte.
        const size_t name_at = is64bit ? offsetof(linux_dirent64, d_name)
                                       : 2 * sizeof(unsigned long) + sizeof(unsigned short);
        const size_t size    = name_at + entry.name.size() + (is64bit ? 1 : 2);
        const auto reclen    = static_cast<unsigned short>((size + 7) & ~static_cast<size_t>(7));
        if (used + reclen > count) {
            break;
        }
        char *record          = buffer + used;
        const ino64_t inode   = entry.inode;
        const off64_t next_at = static_cast<off64_t>(listing.position + 1);
        memset(record, 0, reclen);
        memcpy(record, &inode, sizeof(inode));
        memcpy(record + sizeof(inode), &next_at, sizeof(next_at));
        memcpy(record + sizeof(inode) + sizeof(next_at), &reclen, sizeof(reclen));
        if (is64bit) {
            record[offsetof(linux_dirent64, d_type)] = static_cast<char>(entry.type);
        } else {
            record[reclen - 1] = static_cast<char>(entry.type);
        }
        memcpy(record + name_at, entry.name.data(), entry.name.size());
        used += reclen;
        ++listing.position;
    }
    return used == 0 ? -EINVAL : static_cast<long>(used); // EINVAL: the buffer can't hold one
}

inline int capio_getdents_fs(const int fd, char *buffer, const size_t count, const bool is64bit,
                             const long tid, long *result) {
    const auto shared_listing = capio_listings_fs().get(fd);
    auto &listing             = *shared_listing;
    if (listing.as_is) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    const std::string path = fd_table->path(fd);

    if (!listing.is_started) {
        struct stat info{};
        if (syscall_no_intercept(SYS_fstat, fd, &info) != 0 || !S_ISDIR(info.st_mode)) {
            return CAPIO_POSIX_SYSCALL_SKIP; // not a directory: the kernel returns ENOTDIR
        }
        // In update mode, this already waits for the commit
        const auto how     = list_request_fs(path, -1, tid);
        listing.is_started = true;
        if (how == CAPIO_FS_LIST_AS_IS) {
            listing.as_is = true;
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
        const long n_entries = capio_rescan_fs(fd, listing);
        if (n_entries < 0) {
            *result = n_entries;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        listing.is_final = how == CAPIO_FS_LIST_COMMITTED;
    }

    while (listing.position >= listing.entries.size()) {
        if (listing.is_final) {
            *result = 0;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
        long n_entries = capio_rescan_fs(fd, listing);
        if (n_entries >= 0 && listing.position < listing.entries.size()) {
            break;
        }
        if (n_entries >= 0 && list_request_fs(path, n_entries, tid) == CAPIO_FS_LIST_COMMITTED) {
            // Read once more after the commit, so that no entry created just before it is missed
            n_entries        = capio_rescan_fs(fd, listing);
            listing.is_final = true;
        }
        if (n_entries < 0) {
            *result = n_entries;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    }
    *result = capio_serve_entries_fs(listing, buffer, count, is64bit);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/**
 * FS variant: handle lseek on a directory that CAPIO is listing. This is how glibc implements
 * rewinddir, seekdir and telldir, which must work on CAPIO's listing rather than the kernel's.
 *
 * @return true if @p fd is such a directory, and @p result is set
 */
inline bool capio_seek_listing_fs(const int fd, const off64_t offset, const int whence,
                                  long &result) {
    const auto found = capio_listings_fs().find(fd);
    if (found == nullptr || found->as_is || !found->is_started) {
        return false;
    }
    auto &listing = *found;
    if (whence == SEEK_SET && offset == 0) {
        capio_forget_listing_fs(fd); // rewinddir starts over, from the directory as it is now
        syscall_no_intercept(SYS_lseek, fd, 0, SEEK_SET);
        result = 0;
    } else if (whence == SEEK_SET && offset > 0) {
        listing.position = std::min(static_cast<size_t>(offset), listing.entries.size());
        result           = static_cast<long>(listing.position);
    } else if (whence == SEEK_CUR && offset == 0) {
        result = static_cast<long>(listing.position);
    } else {
        result = -EINVAL;
    }
    return true;
}

// TODO: too similar to capio_read, refactoring needed
inline int getdents_handler_impl(long arg0, long arg1, long arg2, long *result, bool is64bit) {
    auto fd      = static_cast<int>(arg0);
    auto *buffer = reinterpret_cast<struct linux_dirent64 *>(arg1);
    auto count   = static_cast<off64_t>(arg2);
    START_LOG(syscall_no_intercept(SYS_gettid), "call(fd=%d, dirp=0x%08x, count=%ld, is64bit=%s)",
              fd, buffer, count, is64bit ? "true" : "false");

    if (fd_table->contains(fd)) {
        LOG("fd=%d, is a capio file descriptor", fd);
        FS_MODE({
            return capio_getdents_fs(fd, reinterpret_cast<char *>(buffer),
                                     static_cast<size_t>(count), is64bit,
                                     syscall_no_intercept(SYS_gettid), result);
        })

        if (count >= SSIZE_MAX) {
            ERR_EXIT("src does not support read bigger than SSIZE_MAX yet");
        }
        write_cache->flush();
        *result = read_cache->read(fd, buffer, count, true, is64bit);

        DBG(syscall_no_intercept(SYS_gettid), [](char *buf, off64_t count) {
            START_LOG(syscall_no_intercept(SYS_gettid), "call (count=%ld)", count);
            struct linux_dirent64 *d;
            LOG("%25s %12s %13s %15s   %s", "INODE", "TYPE", "RECORD_LENGTH", "OFFSET", "NAME");
            for (off64_t bpos = 0, i = 0; bpos < count && i < 10; i++) {
                d = (struct linux_dirent64 *) (buf + bpos);
                LOG("%25lu %12s %13ld %15ld   %s\n", d->d_ino,
                    (d->d_type == 8)    ? "regular"
                    : (d->d_type == 4)  ? "directory"
                    : (d->d_type == 1)  ? "FIFO"
                    : (d->d_type == 12) ? "socket"
                    : (d->d_type == 10) ? "symlink"
                    : (d->d_type == 6)  ? "block dev"
                    : (d->d_type == 2)  ? "char dev"
                                        : "???",
                    d->d_reclen, d->d_off, d->d_name);
                bpos += d->d_reclen;
            }
        }(reinterpret_cast<char *>(buffer), *result));

        return CAPIO_POSIX_SYSCALL_SUCCESS;
    } else {
        LOG("fd=%d, is not a capio file descriptor", fd);
    }
    return CAPIO_POSIX_SYSCALL_SKIP;
}

inline int getdents_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                            long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, false);
}

inline int getdents64_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                              long *result) {
    return getdents_handler_impl(arg0, arg1, arg2, result, true);
}

#endif // SYS_getdents || SYS_getdents64
#endif // CAPIO_POSIX_HANDLERS_GETDENTS_HPP

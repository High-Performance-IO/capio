#ifndef CAPIO_POSIX_UTILS_FS_LISTING_HPP
#define CAPIO_POSIX_UTILS_FS_LISTING_HPP

#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#include "common/dirent.hpp"
#include "filesystem.hpp"
#include "requests.hpp"

/*
 * getdents of the FS variant.
 *
 * Only on_n_files directories make their consumers wait. The others are listed by the kernel.
 * Waiting cannot be left to the kernel: a descriptor at the end of a directory never sees entries
 * created later. So at the end CAPIO lists the directory again with a new descriptor, serves the
 * names this descriptor has not served yet, and if there are none waits for one or for the commit.
 *
 * - update: wait for the commit, then list everything.
 * - no_update: list entries as they appear, and end at the commit.
 *
 * It is done at system call level, below readdir, so that scandir, glob and nftw, which call
 * readdir inside glibc, wait as well. The position of an entry (d_off) is its index.
 */

struct FsListing {
    struct Entry {
        ino64_t inode;
        unsigned char type;
        std::string name;
    };

    bool is_started = false;
    bool as_is      = false; ///< Listed by the kernel
    bool is_final   = false; ///< Committed, and listed since: there will be nothing else
    std::vector<Entry> entries;
    std::unordered_set<std::string> names;
    size_t position = 0;
};

inline std::unordered_map<int, FsListing> &fs_listings() {
    static auto *listings = new std::unordered_map<int, FsListing>();
    return *listings;
}

/// @brief Forget the listing of a descriptor number
inline void fs_forget_listing(const int fd) {
    CAPIO_DESCRIPTOR_TABLE_LOCK();
    fs_listings().erase(fd);
}

/// @brief The listing of a descriptor. It stays valid until that descriptor is closed.
inline FsListing &fs_listing_of(const int fd) {
    CAPIO_DESCRIPTOR_TABLE_LOCK();
    return fs_listings()[fd];
}

/**
 * List the directory of @p fd again, adding the entries that are new to @p listing.
 *
 * @return the entries in the directory, without "." and "..", or a negated error
 */
inline long fs_rescan(const int fd, FsListing &listing) {
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
            n_entries += name != "." && name != "..";
            if (listing.names.emplace(name).second) {
                listing.entries.push_back({entry->d_ino, entry->d_type, std::string(name)});
            }
        }
    }
    syscall_no_intercept(SYS_close, scan);
    return length < 0 ? length : n_entries;
}

/// @brief Copy the next entries to @p buffer, in the format of getdents or getdents64
inline long fs_serve_entries(FsListing &listing, char *buffer, const size_t count,
                             const bool is64bit) {
    size_t used = 0;
    while (listing.position < listing.entries.size()) {
        const auto &entry    = listing.entries[listing.position];
        // getdents64: ino, off, reclen, type, name. getdents: ino, off, reclen, name, padding,
        // and the type in the last byte of the record.
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
    return used == 0 ? -EINVAL : static_cast<long>(used); // EINVAL: not even one fits
}

inline int fs_getdents(const int fd, char *buffer, const size_t count, const bool is64bit,
                       const long tid, long *result) {
    if (!exists_capio_fd(fd)) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    auto &listing = fs_listing_of(fd);
    if (listing.as_is) {
        return CAPIO_POSIX_SYSCALL_SKIP;
    }
    const std::string path = get_capio_fd_path(fd);

    if (!listing.is_started) {
        struct stat info{};
        if (syscall_no_intercept(SYS_fstat, fd, &info) != 0 || !S_ISDIR(info.st_mode)) {
            return CAPIO_POSIX_SYSCALL_SKIP; // ENOTDIR
        }
        const auto how     = list_request_fs(path, -1, tid); // in update, waits for the commit
        listing.is_started = true;
        if (how == CAPIO_FS_LIST_AS_IS) {
            listing.as_is = true;
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
        const long n_entries = fs_rescan(fd, listing);
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
        long n_entries = fs_rescan(fd, listing);
        if (n_entries >= 0 && listing.position < listing.entries.size()) {
            break;
        }
        if (n_entries >= 0 && list_request_fs(path, n_entries, tid) == CAPIO_FS_LIST_COMMITTED) {
            n_entries        = fs_rescan(fd, listing); // after the commit, so nothing is missed
            listing.is_final = true;
        }
        if (n_entries < 0) {
            *result = n_entries;
            return CAPIO_POSIX_SYSCALL_SUCCESS;
        }
    }
    *result = fs_serve_entries(listing, buffer, count, is64bit);
    return CAPIO_POSIX_SYSCALL_SUCCESS;
}

/**
 * lseek on a directory that CAPIO lists: rewinddir, seekdir and telldir.
 *
 * @return whether it was one
 */
inline bool fs_seek_listing(const int fd, const off64_t offset, const int whence, long &result) {
    CAPIO_DESCRIPTOR_TABLE_LOCK();
    auto &listings   = fs_listings();
    const auto found = listings.find(fd);
    if (found == listings.end() || found->second.as_is || !found->second.is_started) {
        return false;
    }
    auto &listing = found->second;
    if (whence == SEEK_SET && offset == 0) {
        listings.erase(found); // rewinddir shows the directory as it is now
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

#endif // CAPIO_POSIX_UTILS_FS_LISTING_HPP

#include "utils/file_descriptor_table.hpp"

#include <climits>
#include <cstring>
#include <new>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

#include "common/constants.hpp"

namespace {

/*
 * The snapshot: a file in /dev/shm, named after the thread that execs, holding the number of
 * descriptors, then one entry for each. Its system calls go to the kernel through
 * syscall_no_intercept, as CAPIO would handle them otherwise, and its handlers use the table.
 */
std::string snapshotPath(const long tid) { return SHM_SNAPSHOT_PREFIX + std::to_string(tid); }

struct SnapshotEntry {
    int fd;
    int copy_of; ///< the first entry that shares its offset (dup): its own index if none
    off64_t offset;
    off64_t size;
    int flags;
    bool cloexec;
    char path[PATH_MAX];
};

struct SnapshotHeader {
    size_t count;
};

size_t snapshotLength(const size_t count) {
    return sizeof(SnapshotHeader) + count * sizeof(SnapshotEntry);
}

/// @brief mmap of @p length bytes of @p fd, nullptr if it fails
void *mapSnapshot(const long fd, const size_t length, const int protection) {
    const long address =
        syscall_no_intercept(SYS_mmap, nullptr, length, protection, MAP_SHARED, fd, 0);
    return address < 0 && address > -4096 ? nullptr : reinterpret_cast<void *>(address);
}

void removeSnapshot(const std::string &path) {
    syscall_no_intercept(SYS_unlinkat, AT_FDCWD, path.c_str(), 0);
}

} // namespace

FileDescriptorTable::FileDescriptorTable(const long tid) {
    const auto path = snapshotPath(tid);
    const long file =
        syscall_no_intercept(SYS_openat, AT_FDCWD, path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        return;
    }
    removeSnapshot(path); // read once: a later exec writes its own
    struct stat info{};
    void *memory = nullptr;
    if (syscall_no_intercept(SYS_fstat, file, &info) == 0 &&
        static_cast<size_t>(info.st_size) >= sizeof(SnapshotHeader)) {
        memory = mapSnapshot(file, info.st_size, PROT_READ);
    }
    syscall_no_intercept(SYS_close, file);
    if (memory == nullptr) {
        return;
    }
    const auto *header  = static_cast<const SnapshotHeader *>(memory);
    const auto *entries = reinterpret_cast<const SnapshotEntry *>(header + 1);
    if (snapshotLength(header->count) <= static_cast<size_t>(info.st_size)) {
        std::unordered_map<int, int> restored; // entry index -> its descriptor in the table
        for (size_t i = 0; i < header->count; ++i) {
            const SnapshotEntry &entry = entries[i];
            if (entry.cloexec) {
                continue; // closed by the exec
            }
            const auto original = restored.find(entry.copy_of);
            if (original != restored.end()) {
                dup(original->second, entry.fd, false);
            } else {
                add(entry.fd, entry.path, entry.offset, entry.size, entry.flags, false);
                restored.emplace(entry.copy_of, entry.fd);
            }
        }
    }
    syscall_no_intercept(SYS_munmap, memory, info.st_size);
}

void FileDescriptorTable::createSnapshot(const long tid) const {
    const std::shared_lock lock(mutex());
    const auto path = snapshotPath(tid);
    if (_descriptors.empty()) {
        removeSnapshot(path); // one left by an exec that failed
        return;
    }
    const auto length = snapshotLength(_descriptors.size());
    const long file =
        syscall_no_intercept(SYS_openat, AT_FDCWD, path.c_str(),
                             O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (file < 0) {
        return;
    }
    void *memory = nullptr;
    if (syscall_no_intercept(SYS_ftruncate, file, length) == 0) {
        memory = mapSnapshot(file, length, PROT_READ | PROT_WRITE);
    }
    syscall_no_intercept(SYS_close, file);
    if (memory == nullptr) {
        removeSnapshot(path);
        return;
    }
    auto *header  = static_cast<SnapshotHeader *>(memory);
    auto *entries = reinterpret_cast<SnapshotEntry *>(header + 1);
    header->count = _descriptors.size();
    size_t i      = 0;
    std::unordered_map<const off64_t *, int> first_with_offset;
    for (const auto &[fd, descriptor] : _descriptors) {
        SnapshotEntry &entry = entries[i];
        entry.fd             = fd;
        entry.copy_of = first_with_offset.try_emplace(descriptor.offset.get(), static_cast<int>(i))
                            .first->second;
        entry.offset  = *descriptor.offset;
        entry.size    = descriptor.size;
        entry.flags   = descriptor.flags;
        entry.cloexec = descriptor.cloexec;
        strncpy(entry.path, descriptor.path.c_str(), PATH_MAX - 1);
        entry.path[PATH_MAX - 1] = '\0';
        ++i;
    }
    syscall_no_intercept(SYS_munmap, memory, length);
}

void FileDescriptorTable::_remove(const int fd) {
    const auto descriptor = _descriptors.find(fd);
    if (descriptor == _descriptors.end()) {
        return;
    }
    if (const auto path = _paths.find(descriptor->second.path); path != _paths.end()) {
        path->second.erase(fd);
    }
    _descriptors.erase(descriptor);
}

std::shared_mutex &FileDescriptorTable::mutex() {
    // Made at its first use: the table is used before the constructors of the library have run
    static std::shared_mutex mutex;
    return mutex;
}

void FileDescriptorTable::protectAcrossFork() {
    // The lock is held during the fork, so that the child gets a whole table. The child cannot
    // unlock it: the lock knows its owner by thread id, which the child has a new one of. So the
    // child gets a new lock, in the place of the old one.
    pthread_atfork([] { mutex().lock(); }, [] { mutex().unlock(); },
                   [] { new (&mutex()) std::shared_mutex(); });
}

void FileDescriptorTable::addPath(const std::string &path) {
    const std::lock_guard lock(mutex());
    _paths.try_emplace(path);
}

void FileDescriptorTable::add(const int fd, const std::string &path, const off64_t offset,
                              const off64_t size, const int flags, const bool cloexec) {
    const std::lock_guard lock(mutex());
    _remove(fd);
    _paths[path].insert(fd);
    _descriptors.insert_or_assign(
        fd, Descriptor{path, std::make_shared<off64_t>(offset), size, flags, cloexec});
}

void FileDescriptorTable::dup(const int old_fd, const int new_fd, const bool cloexec) {
    const std::lock_guard lock(mutex());
    Descriptor copy = _descriptors.at(old_fd);
    copy.cloexec    = cloexec;
    _remove(new_fd);
    _paths[copy.path].insert(new_fd);
    _descriptors.insert_or_assign(new_fd, std::move(copy));
}

void FileDescriptorTable::remove(const int fd) {
    const std::lock_guard lock(mutex());
    _remove(fd);
}

long FileDescriptorTable::close(const int fd) {
    const std::lock_guard lock(mutex());
    _remove(fd);
    return syscall_no_intercept(SYS_close, fd);
}

void FileDescriptorTable::removePath(const std::string &path) {
    const std::lock_guard lock(mutex());
    const auto found = _paths.find(path);
    if (found == _paths.end()) {
        return;
    }
    for (const int fd : found->second) {
        _descriptors.erase(fd);
    }
    _paths.erase(found);
}

void FileDescriptorTable::renamePath(const std::string &old_path, const std::string &new_path) {
    const std::lock_guard lock(mutex());
    auto entry = _paths.extract(old_path);
    if (entry.empty()) {
        return;
    }
    entry.key() = new_path;
    for (const int fd : entry.mapped()) {
        _descriptors.at(fd).path = new_path;
    }
    _paths.insert(std::move(entry));
}

bool FileDescriptorTable::contains(const int fd) const {
    const std::shared_lock lock(mutex());
    return _descriptors.find(fd) != _descriptors.end();
}

bool FileDescriptorTable::containsPath(const std::string &path) const {
    const std::shared_lock lock(mutex());
    return _paths.find(path) != _paths.end();
}

std::vector<int> FileDescriptorTable::fds() const {
    const std::shared_lock lock(mutex());
    std::vector<int> fds;
    fds.reserve(_descriptors.size());
    for (const auto &[fd, _] : _descriptors) {
        fds.push_back(fd);
    }
    return fds;
}

std::string FileDescriptorTable::path(const int fd) const {
    const std::shared_lock lock(mutex());
    return _descriptors.at(fd).path;
}

std::optional<std::string> FileDescriptorTable::findPath(const int fd) const {
    const std::shared_lock lock(mutex());
    const auto found = _descriptors.find(fd);
    return found == _descriptors.end() ? std::nullopt : std::optional{found->second.path};
}

off64_t FileDescriptorTable::offset(const int fd) const {
    const std::shared_lock lock(mutex());
    return *_descriptors.at(fd).offset;
}

void FileDescriptorTable::setOffset(const int fd, const off64_t offset) {
    const std::lock_guard lock(mutex());
    *_descriptors.at(fd).offset = offset;
}

bool FileDescriptorTable::sharesOffset(const int fd, const int other) const {
    const std::shared_lock lock(mutex());
    return _descriptors.at(fd).offset == _descriptors.at(other).offset;
}

off64_t FileDescriptorTable::size(const int fd) const {
    const std::shared_lock lock(mutex());
    return _descriptors.at(fd).size;
}

int FileDescriptorTable::flags(const int fd) const {
    const std::shared_lock lock(mutex());
    return _descriptors.at(fd).flags;
}

void FileDescriptorTable::setFlags(const int fd, const int flags) {
    const std::lock_guard lock(mutex());
    _descriptors.at(fd).flags = flags;
}

bool FileDescriptorTable::cloexec(const int fd) const {
    const std::shared_lock lock(mutex());
    return _descriptors.at(fd).cloexec;
}

void FileDescriptorTable::setCloexec(const int fd, const bool cloexec) {
    const std::lock_guard lock(mutex());
    _descriptors.at(fd).cloexec = cloexec;
}

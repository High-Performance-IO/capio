#include "utils/file_descriptor_table.hpp"

#include <climits>
#include <cstring>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

namespace {

/// @brief The lock of the tables: one, as a process has one table of descriptors
pthread_mutex_t table_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

struct Lock {
    Lock() { pthread_mutex_lock(&table_mutex); }
    ~Lock() { pthread_mutex_unlock(&table_mutex); }
    Lock(const Lock &)            = delete;
    Lock &operator=(const Lock &) = delete;
};

/*
 * The snapshot: one shared memory object, named after the thread that execs, holding the number
 * of descriptors, then one entry for each.
 */
std::string snapshotName(const long tid) { return "/capio_snapshot_" + std::to_string(tid); }

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

} // namespace

FileDescriptorTable::FileDescriptorTable(const long tid) {
    const auto name = snapshotName(tid);
    const int shm   = shm_open(name.c_str(), O_RDONLY, 0);
    if (shm < 0) {
        return;
    }
    shm_unlink(name.c_str()); // read once: a later exec writes its own
    struct stat info{};
    void *memory = MAP_FAILED;
    if (fstat(shm, &info) == 0 && static_cast<size_t>(info.st_size) >= sizeof(SnapshotHeader)) {
        memory = mmap(nullptr, info.st_size, PROT_READ, MAP_SHARED, shm, 0);
    }
    ::close(shm);
    if (memory == MAP_FAILED) {
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
    munmap(memory, info.st_size);
}

void FileDescriptorTable::createSnapshot(const long tid) const {
    const Lock lock;
    const auto name = snapshotName(tid);
    if (_descriptors.empty()) {
        shm_unlink(name.c_str()); // one left by an exec that failed
        return;
    }
    const auto length = snapshotLength(_descriptors.size());
    const int shm     = shm_open(name.c_str(), O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    if (shm < 0) {
        return;
    }
    void *memory = MAP_FAILED;
    if (ftruncate(shm, static_cast<off_t>(length)) == 0) {
        memory = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    }
    ::close(shm);
    if (memory == MAP_FAILED) {
        shm_unlink(name.c_str());
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
    munmap(memory, length);
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

void FileDescriptorTable::protectAcrossFork() {
    pthread_atfork([] { pthread_mutex_lock(&table_mutex); },
                   [] { pthread_mutex_unlock(&table_mutex); },
                   [] { table_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP; });
}

void FileDescriptorTable::addPath(const std::string &path) {
    const Lock lock;
    _paths.try_emplace(path);
}

void FileDescriptorTable::add(const int fd, const std::string &path, const off64_t offset,
                              const off64_t size, const int flags, const bool cloexec) {
    const Lock lock;
    _remove(fd);
    _paths[path].insert(fd);
    _descriptors.insert_or_assign(
        fd, Descriptor{path, std::make_shared<off64_t>(offset), size, flags, cloexec});
}

void FileDescriptorTable::dup(const int old_fd, const int new_fd, const bool cloexec) {
    const Lock lock;
    Descriptor copy = _descriptors.at(old_fd);
    copy.cloexec    = cloexec;
    _remove(new_fd);
    _paths[copy.path].insert(new_fd);
    _descriptors.insert_or_assign(new_fd, std::move(copy));
}

void FileDescriptorTable::remove(const int fd) {
    const Lock lock;
    _remove(fd);
}

long FileDescriptorTable::close(const int fd) {
    const Lock lock;
    _remove(fd);
    return syscall_no_intercept(SYS_close, fd);
}

void FileDescriptorTable::removePath(const std::string &path) {
    const Lock lock;
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
    const Lock lock;
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
    const Lock lock;
    return _descriptors.find(fd) != _descriptors.end();
}

bool FileDescriptorTable::containsPath(const std::string &path) const {
    const Lock lock;
    return _paths.find(path) != _paths.end();
}

std::vector<int> FileDescriptorTable::fds() const {
    const Lock lock;
    std::vector<int> fds;
    fds.reserve(_descriptors.size());
    for (const auto &[fd, _] : _descriptors) {
        fds.push_back(fd);
    }
    return fds;
}

std::string FileDescriptorTable::path(const int fd) const {
    const Lock lock;
    return _descriptors.at(fd).path;
}

std::optional<std::string> FileDescriptorTable::findPath(const int fd) const {
    const Lock lock;
    const auto found = _descriptors.find(fd);
    return found == _descriptors.end() ? std::nullopt : std::optional{found->second.path};
}

off64_t FileDescriptorTable::offset(const int fd) const {
    const Lock lock;
    return *_descriptors.at(fd).offset;
}

void FileDescriptorTable::setOffset(const int fd, const off64_t offset) {
    const Lock lock;
    *_descriptors.at(fd).offset = offset;
}

bool FileDescriptorTable::sharesOffset(const int fd, const int other) const {
    const Lock lock;
    return _descriptors.at(fd).offset == _descriptors.at(other).offset;
}

off64_t FileDescriptorTable::size(const int fd) const {
    const Lock lock;
    return _descriptors.at(fd).size;
}

int FileDescriptorTable::flags(const int fd) const {
    const Lock lock;
    return _descriptors.at(fd).flags;
}

void FileDescriptorTable::setFlags(const int fd, const int flags) {
    const Lock lock;
    _descriptors.at(fd).flags = flags;
}

bool FileDescriptorTable::cloexec(const int fd) const {
    const Lock lock;
    return _descriptors.at(fd).cloexec;
}

void FileDescriptorTable::setCloexec(const int fd, const bool cloexec) {
    const Lock lock;
    _descriptors.at(fd).cloexec = cloexec;
}

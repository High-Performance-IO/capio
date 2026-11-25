#ifndef CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP
#define CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <pthread.h>
#include <sys/types.h>

#include "common/syscall.hpp"

/**
 * @brief The system calls that FileDescriptorTable makes, through syscall_no_intercept. Tests give
 * the table another class with the same methods instead (see BasicFileDescriptorTable).
 */
class SystemCalls final {
  public:
    /// @return what close(2) returned: 0, or -errno
    long close(const int fd) { return syscall_no_intercept(SYS_close, fd); }

    /// @brief The system calls of the kernel
    static SystemCalls &kernel() {
        static SystemCalls system_calls;
        return system_calls;
    }
};

/**
 * @brief The CAPIO files of a process: which descriptors are open on which paths, with their
 * offset, size, flags and close-on-exec.
 *
 * The threads of a process share their descriptors, so every method takes the lock of the table:
 * the callers never lock. The lock is recursive, as allocating can make system calls, whose
 * handlers use the table. fork takes it (see protectAcrossFork), so that the child gets a whole
 * table and a free lock.
 *
 * The getters of one descriptor (path, offset, ...) throw std::out_of_range if it is not in the
 * table: check with contains first, or use findPath.
 *
 * @tparam Kernel the system calls the table makes: SystemCalls, or a mock of it in the tests. A
 * template parameter and not a virtual class, so that the table has no virtual calls.
 */
template <class Kernel> class BasicFileDescriptorTable final {
    struct Descriptor {
        std::string path;
        std::shared_ptr<off64_t> offset; ///< shared by the copies of a descriptor (dup)
        off64_t size;
        int flags;
        bool cloexec;
    };

    std::unordered_map<int, Descriptor> _descriptors;
    std::unordered_map<std::string, std::unordered_set<int>> _paths; ///< path -> its descriptors
    Kernel &_kernel;

    static inline pthread_mutex_t _mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

    struct Lock {
        Lock() { pthread_mutex_lock(&_mutex); }
        ~Lock() { pthread_mutex_unlock(&_mutex); }
        Lock(const Lock &)            = delete;
        Lock &operator=(const Lock &) = delete;
    };

    void _remove(const int fd) {
        const auto descriptor = _descriptors.find(fd);
        if (descriptor == _descriptors.end()) {
            return;
        }
        if (const auto path = _paths.find(descriptor->second.path); path != _paths.end()) {
            path->second.erase(fd);
        }
        _descriptors.erase(descriptor);
    }

  public:
    explicit BasicFileDescriptorTable(Kernel &kernel = Kernel::kernel()) : _kernel(kernel) {}

    /// @brief Lock the table across fork(): the child gets a free lock, whatever its other threads
    /// were doing. Call once, before there are threads.
    static void protectAcrossFork() {
        pthread_atfork([] { pthread_mutex_lock(&_mutex); }, [] { pthread_mutex_unlock(&_mutex); },
                       [] { _mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP; });
    }

    /// @brief Know @p path, also with no descriptor open on it (e.g. a directory made by mkdir)
    void addPath(const std::string &path) {
        const Lock lock;
        _paths.try_emplace(path);
    }

    /// @brief Add the descriptor @p fd of @p path, in place of what @p fd was
    void add(const int fd, const std::string &path, const off64_t offset, const off64_t size,
             const int flags, const bool cloexec) {
        const Lock lock;
        _remove(fd);
        _paths[path].insert(fd);
        _descriptors.insert_or_assign(
            fd, Descriptor{path, std::make_shared<off64_t>(offset), size, flags, cloexec});
    }

    /// @brief Make @p new_fd a copy of @p old_fd, sharing its offset, in place of what @p new_fd
    /// was
    void dup(const int old_fd, const int new_fd, const bool cloexec) {
        const Lock lock;
        Descriptor copy = _descriptors.at(old_fd);
        copy.cloexec    = cloexec;
        _remove(new_fd);
        _paths[copy.path].insert(new_fd);
        _descriptors.insert_or_assign(new_fd, std::move(copy));
    }

    /// @brief Forget the descriptor @p fd, if it is in the table
    void remove(const int fd) {
        const Lock lock;
        _remove(fd);
    }

    /**
     * @brief Close the descriptor @p fd in the kernel and forget it. It is forgotten also if the
     * close fails: Linux frees the number anyway, except for a number that is not open (EBADF).
     * The lock is held during the close, so that no other thread sees the number in the table once
     * the kernel can give it to someone else.
     * @return what close(2) returned: 0, or -errno
     */
    long close(const int fd) {
        const Lock lock;
        _remove(fd);
        return _kernel.close(fd);
    }

    /// @brief Forget @p path and all its descriptors
    void removePath(const std::string &path) {
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

    /// @brief Rename @p old_path, and the descriptors open on it, to @p new_path
    void renamePath(const std::string &old_path, const std::string &new_path) {
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

    [[nodiscard]] bool contains(const int fd) const {
        const Lock lock;
        return _descriptors.find(fd) != _descriptors.end();
    }

    [[nodiscard]] bool containsPath(const std::string &path) const {
        const Lock lock;
        return _paths.find(path) != _paths.end();
    }

    /// @brief All the descriptors in the table
    [[nodiscard]] std::vector<int> fds() const {
        const Lock lock;
        std::vector<int> fds;
        fds.reserve(_descriptors.size());
        for (const auto &[fd, _] : _descriptors) {
            fds.push_back(fd);
        }
        return fds;
    }

    /// @return a copy: another thread may close @p fd meanwhile
    [[nodiscard]] std::string path(const int fd) const {
        const Lock lock;
        return _descriptors.at(fd).path;
    }

    /// @brief The path of @p fd, if it is in the table
    [[nodiscard]] std::optional<std::string> findPath(const int fd) const {
        const Lock lock;
        const auto found = _descriptors.find(fd);
        return found == _descriptors.end() ? std::nullopt : std::optional{found->second.path};
    }

    [[nodiscard]] off64_t offset(const int fd) const {
        const Lock lock;
        return *_descriptors.at(fd).offset;
    }

    void setOffset(const int fd, const off64_t offset) {
        const Lock lock;
        *_descriptors.at(fd).offset = offset;
    }

    /// @brief Whether @p fd and @p other are copies of each other, that share their offset
    [[nodiscard]] bool sharesOffset(const int fd, const int other) const {
        const Lock lock;
        return _descriptors.at(fd).offset == _descriptors.at(other).offset;
    }

    [[nodiscard]] off64_t size(const int fd) const {
        const Lock lock;
        return _descriptors.at(fd).size;
    }

    [[nodiscard]] int flags(const int fd) const {
        const Lock lock;
        return _descriptors.at(fd).flags;
    }

    void setFlags(const int fd, const int flags) {
        const Lock lock;
        _descriptors.at(fd).flags = flags;
    }

    [[nodiscard]] bool cloexec(const int fd) const {
        const Lock lock;
        return _descriptors.at(fd).cloexec;
    }

    void setCloexec(const int fd, const bool cloexec) {
        const Lock lock;
        _descriptors.at(fd).cloexec = cloexec;
    }
};

/// @brief The table of the descriptors of the process, which makes real system calls
using FileDescriptorTable = BasicFileDescriptorTable<SystemCalls>;

#endif // CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP

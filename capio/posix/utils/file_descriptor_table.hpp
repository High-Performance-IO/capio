#ifndef CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP
#define CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP

#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/types.h>

/**
 * @brief The CAPIO files of a process: which descriptors are open on which paths, with their
 * offset, size, flags and close-on-exec. Implemented in src/file_descriptor_table.cpp.
 *
 * The threads of a process share their descriptors, so every method takes the lock of the table,
 * shared to read and exclusive to change it: the callers never lock. The lock is not recursive, so
 * a method holding it neither calls another one nor makes a system call that CAPIO handles, whose
 * handler would use the table. fork takes it (see protectAcrossFork), so that the child gets a
 * whole table and a free lock.
 *
 * The getters of one descriptor (path, offset, ...) throw std::out_of_range if it is not in the
 * table: check with contains first, or use findPath.
 *
 * execve replaces the memory of the process, table included, but keeps its descriptors. So before
 * an exec the table writes a snapshot of itself to shared memory (createSnapshot), and the program
 * after it builds its table from that snapshot (FileDescriptorTable(tid)).
 */
class FileDescriptorTable final {
    struct Descriptor {
        std::string path;
        std::shared_ptr<off64_t> offset; ///< shared by the copies of a descriptor (dup)
        off64_t size;
        int flags;
        bool cloexec;
    };

    std::unordered_map<int, Descriptor> _descriptors;
    std::unordered_map<std::string, std::unordered_set<int>> _paths; ///< path -> its descriptors

    /// @brief The lock of the tables: one, as a process has one table of descriptors
    static std::shared_mutex &mutex();

    void _remove(int fd);

  public:
    FileDescriptorTable() = default;

    /**
     * @brief The table of the program that thread @p tid has execed: the one in the snapshot that
     * the thread wrote before the exec, without the descriptors that the exec closed
     * (close-on-exec). The snapshot is removed. Empty if there is none: a program that did not
     * start with an exec, or whose previous program had no CAPIO file open.
     */
    explicit FileDescriptorTable(long tid);

    /**
     * @brief Write the table to a snapshot for the program that thread @p tid is about to exec. It
     * replaces any snapshot of @p tid left by an exec that failed. Nothing is written if the table
     * is empty, or if the snapshot cannot be made: the next program then starts with no CAPIO file.
     * Written under the lock, so that it is the table of one moment.
     */
    void createSnapshot(long tid) const;

    /// @brief Lock the tables across fork(): the child gets a free lock, whatever its other threads
    /// were doing. Call once, before there are threads.
    static void protectAcrossFork();

    /// @brief Know @p path, also with no descriptor open on it (e.g. a directory made by mkdir)
    void addPath(const std::string &path);

    /// @brief Add the descriptor @p fd of @p path, in place of what @p fd was
    void add(int fd, const std::string &path, off64_t offset, off64_t size, int flags,
             bool cloexec);

    /// @brief Make @p new_fd a copy of @p old_fd, sharing its offset, in place of what @p new_fd
    /// was
    void dup(int old_fd, int new_fd, bool cloexec);

    /// @brief Forget the descriptor @p fd, if it is in the table
    void remove(int fd);

    /**
     * @brief Close the descriptor @p fd in the kernel and forget it. It is forgotten also if the
     * close fails: Linux frees the number anyway, except for a number that is not open (EBADF).
     * The lock is held during the close, so that no other thread sees the number in the table once
     * the kernel can give it to someone else.
     * @return what close(2) returned: 0, or -errno
     */
    long close(int fd);

    /// @brief Forget @p path and all its descriptors
    void removePath(const std::string &path);

    /// @brief Rename @p old_path, and the descriptors open on it, to @p new_path
    void renamePath(const std::string &old_path, const std::string &new_path);

    [[nodiscard]] bool contains(int fd) const;

    [[nodiscard]] bool containsPath(const std::string &path) const;

    /// @brief All the descriptors in the table
    [[nodiscard]] std::vector<int> fds() const;

    /// @return a copy: another thread may close @p fd meanwhile
    [[nodiscard]] std::string path(int fd) const;

    /// @brief The path of @p fd, if it is in the table
    [[nodiscard]] std::optional<std::string> findPath(int fd) const;

    [[nodiscard]] off64_t offset(int fd) const;

    void setOffset(int fd, off64_t offset);

    /// @brief Whether @p fd and @p other are copies of each other, that share their offset
    [[nodiscard]] bool sharesOffset(int fd, int other) const;

    [[nodiscard]] off64_t size(int fd) const;

    [[nodiscard]] int flags(int fd) const;

    void setFlags(int fd, int flags);

    [[nodiscard]] bool cloexec(int fd) const;

    void setCloexec(int fd, bool cloexec);
};

#endif // CAPIO_POSIX_UTILS_FILE_DESCRIPTOR_TABLE_HPP

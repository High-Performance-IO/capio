#ifndef CAPIO_FS_STORAGE_MANAGER_HPP
#define CAPIO_FS_STORAGE_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/types.h>

/**
 * @brief Manages the files of the file system variant (libcapio_posix_fs.so).
 *
 * Its files stay on the file system, so unlike StorageManager it holds no data: it knows what has
 * happened to each file (closes, failed closes, renames) and applies the CAPIO-CL rules to it. As
 * StorageManager, it knows the descriptors of its clients by number. CAPIO-CL keeps the rules and
 * the commit state; ClientManager keeps who produces what. Used by the request thread only, apart
 * from the static methods.
 */
class FsStorageManager {
    /// @brief path -> successful closes that counted, whatever its rule: a rename carries them
    std::unordered_map<std::string, long> _close_counts;

    /// @brief path -> errno of a close that failed: data was lost, never committed by this server
    std::unordered_map<std::string, int> _failed_closes;

    /**
     * @brief pid -> fd -> path: the descriptors of each client process, like the per-thread ones
     * of StorageManager. By process, as its threads share them, and as an exec keeps them.
     */
    std::unordered_map<pid_t, std::unordered_map<int, std::string>> _opened_fd_map;

    /// @brief tid -> first half of a rename, and whether it is inside CAPIO_DIR
    std::unordered_map<pid_t, std::pair<std::string, bool>> _renaming;

    /// @brief Closes of on_close:N being counted on background threads
    std::atomic<int> _pending_counts{0};

    /// @brief Apply on_close to @p n_closes more closes of @p path. Never blocks.
    void applyOnClose(const std::filesystem::path &path, long n_closes);

    void countClose(const std::filesystem::path &path);

  public:
    FsStorageManager();
    ~FsStorageManager();

    /// @brief Whether @p tid produces @p path: declared in the config, or registered at runtime
    [[nodiscard]] static bool isProducer(pid_t tid, const std::filesystem::path &path);

    /**
     * @brief Whether @p tid waits for the missing @p path to be created.
     *
     * With a config, only its declared consumers do, so that probing for optional files gets
     * ENOENT. Without one nothing tells an input from an output: all but the producer wait.
     */
    [[nodiscard]] static bool waitsForCreation(pid_t tid, const std::filesystem::path &path);

    /**
     * @brief Whether the config mentions @p path, itself or through a glob. Without a config,
     * every path is: it is committed when its producer ends, whoever that is.
     *
     * Not Engine::contains(): CAPIO-CL makes an entry for every path it is asked about, so that
     * turns true after any lookup. Such entries have no apps unless they match a glob.
     */
    [[nodiscard]] static bool isDeclared(const std::filesystem::path &path);

    /**
     * @brief Commit @p path, unless a close of it failed. Never blocks: CAPIO-CL's multicast
     * monitor sleeps 300 ms before answering "not committed", so nothing on the request thread may
     * ask; committing only sends a message.
     */
    void commit(const std::filesystem::path &path);

    /// @brief Process @p pid has the descriptor @p fd of @p path. It replaces what @p fd was, if a
    /// call CAPIO does not handle closed it.
    void addFileToPid(pid_t pid, int fd, const std::filesystem::path &path);

    /// @brief Process @p pid made @p new_fd a copy of @p old_fd
    void dup(pid_t pid, int old_fd, int new_fd);

    /// @brief The path of the descriptor @p fd of process @p pid, empty if it has none
    [[nodiscard]] std::string getPath(pid_t pid, int fd) const;

    /**
     * @brief Thread @p tid of process @p pid closed its descriptor @p fd
     * @param error what the close failed with, 0 if it worked
     */
    void closed(pid_t tid, pid_t pid, int fd, int error);

    /// @brief A descriptor of @p path was closed successfully by @p tid
    void closed(pid_t tid, const std::filesystem::path &path);

    /// @brief A close of @p path failed, so data was lost
    void closeFailed(pid_t tid, const std::filesystem::path &path, int error);

    /// @brief First half of a rename: the old name, and whether it is inside CAPIO_DIR
    void renameFrom(pid_t tid, std::string path, bool in_capio_dir);

    /**
     * @brief Second half of a rename, which worked: what is known of the file, or of the files in
     * the directory, moves to the new name, whose rule is then applied. So do the descriptors open
     * on them.
     */
    void renameTo(pid_t tid, const std::string &path, bool in_capio_dir);

    /// @brief @p tid exited
    void removeThread(pid_t tid);

    /// @brief Block until @p path exists
    void waitForCreation(const std::filesystem::path &path) const;

    /// @brief The size of @p path if its first @p end_of_read bytes can be read now
    [[nodiscard]] static std::optional<off64_t> readable(const std::filesystem::path &path,
                                                         off64_t end_of_read);

    /// @brief Block until the first @p end_of_read bytes of @p path can be read
    /// @return their size, or -1 if @p path was committed with less
    [[nodiscard]] off64_t waitForData(const std::filesystem::path &path, off64_t end_of_read) const;

    /**
     * @brief Block until @p path exists and, if it is declared, is committed
     * @return 1 if that holds from now on, 0 if @p path is not declared
     */
    [[nodiscard]] off64_t waitForConsent(const std::filesystem::path &path) const;

    /// @brief Wait, at most @p timeout, for the closes still being counted: a count lost at exit
    /// would leave the file uncommitted for the servers that start later
    void waitForPendingCounts(std::chrono::milliseconds timeout) const;
};

#endif // CAPIO_FS_STORAGE_MANAGER_HPP

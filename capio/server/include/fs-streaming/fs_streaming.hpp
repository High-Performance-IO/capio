#ifndef CAPIO_SERVER_FS_STREAMING_HPP
#define CAPIO_SERVER_FS_STREAMING_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/types.h>

/// @brief How a descriptor was closed
enum class CloseKind {
    EXPLICIT, ///< close(), by whoever has the descriptor
    IMPLICIT  ///< By the kernel: exit, close-on-exec, dup2 over it, close_range
};

/**
 * @brief What the server knows about the clients of the FS variant and their files.
 *
 * The files are real, so nothing here holds data: this is who produces what. CAPIO-CL keeps the
 * rules and the commit state. Only the request thread uses this class: what a waiter thread needs
 * is computed before starting it.
 */
class FsStreaming final {
    struct Thread {
        pid_t pid;
        uint64_t process; ///< Serial number of the process, as pids are reused
        std::string app;
    };

    std::unordered_map<pid_t, Thread> threads;
    std::unordered_map<pid_t, uint64_t> processes;
    uint64_t last_process = 0;

    /// @brief path -> who produces it: an app or, for apps without a name, a process
    std::unordered_map<std::string, std::unordered_set<std::string>> producers;

    /// @brief path -> processes that opened it: only their implicit closes count
    std::unordered_map<std::string, std::unordered_set<uint64_t>> openers;

    /// @brief path -> successful closes that counted, whatever its rule: a rename carries them
    std::unordered_map<std::string, long> closes;

    /// @brief path -> threads that produced it, which commit it on_termination when they end
    std::unordered_map<std::string, std::unordered_set<pid_t>> producer_threads;

    /// @brief tid -> first half of a rename, and whether it is inside CAPIO_DIR
    std::unordered_map<pid_t, std::pair<std::string, bool>> renaming;

    void applyOnClose(const std::filesystem::path &path, long n_closes);

    /// @brief path -> errno of the close that failed: never committed by this server
    std::unordered_map<std::string, int> failed;

    /// @brief process -> what its execve closes, if it works
    std::unordered_map<uint64_t, std::vector<std::pair<std::string, bool>>> closes_at_exec;

    std::unordered_map<pid_t, std::string> path_parts;

    [[nodiscard]] std::string producerName(pid_t tid) const;

  public:
    static FsStreaming &get();

    void addThread(pid_t tid, pid_t pid, const std::string &app);
    void removeThread(pid_t tid);

    void addProducer(pid_t tid, const std::filesystem::path &path);
    [[nodiscard]] bool isProducer(pid_t tid, const std::filesystem::path &path) const;

    void addOpener(pid_t tid, const std::filesystem::path &path);

    /// @brief A descriptor of @p path was closed successfully
    void closed(pid_t tid, const std::filesystem::path &path, CloseKind kind, bool writable);

    /// @brief A close of @p path failed, so data was lost
    void closeFailed(pid_t tid, const std::filesystem::path &path, int error);

    void closeAtExec(pid_t tid, const std::filesystem::path &path, bool writable);
    void execFailed(pid_t tid);

    /// @brief The threads of the process of @p tid, other than @p tid
    [[nodiscard]] std::vector<pid_t> otherThreads(pid_t tid) const;

    void renameFrom(pid_t tid, std::string path, bool in_capio_dir);

    /**
     * @brief A rename worked: what is known of the file, or of the files in the directory, moves
     * to the new name, whose rule is then evaluated.
     */
    void renameTo(pid_t tid, const std::string &path, bool in_capio_dir);

    /// @brief Keep a piece of a path that did not fit in a request
    void addPathPart(pid_t tid, const char *part);

    /// @brief The path of a request, whose last piece is @p tail
    std::string takePath(pid_t tid, const char *tail);

    /// @brief Commit @p path. Never blocks.
    void commit(const std::filesystem::path &path);

    /// @brief Apply on_close to one more close of @p path. Never blocks.
    void countClose(const std::filesystem::path &path);

    /**
     * @brief Whether @p tid waits for the missing @p path to be created.
     *
     * With a config, only its declared consumers do, so that probing for optional files gets
     * ENOENT. Without one nothing tells an input from an output: all but the producer wait.
     */
    [[nodiscard]] bool waitsForCreation(pid_t tid, const std::filesystem::path &path) const;

    /**
     * @brief Whether the config mentions @p path, itself or through a glob.
     *
     * Not Engine::contains(): CAPIO-CL makes an entry for every path it is asked about, so that
     * turns true after any lookup. Such entries have no apps unless they match a glob.
     */
    static bool isDeclared(const std::filesystem::path &path);

    /// @brief Whether a reader may have to wait for the commit of @p path, which exists
    static bool canWaitForCommit(const std::filesystem::path &path);
};

#endif // CAPIO_SERVER_FS_STREAMING_HPP

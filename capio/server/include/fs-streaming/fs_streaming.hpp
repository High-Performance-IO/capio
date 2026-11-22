#ifndef CAPIO_SERVER_FS_STREAMING_HPP
#define CAPIO_SERVER_FS_STREAMING_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <sys/types.h>

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

    std::unordered_map<pid_t, std::string> path_parts;

    [[nodiscard]] std::string producerName(pid_t tid) const;

  public:
    static FsStreaming &get();

    void addThread(pid_t tid, pid_t pid, const std::string &app);
    void removeThread(pid_t tid);

    void addProducer(pid_t tid, const std::filesystem::path &path);
    [[nodiscard]] bool isProducer(pid_t tid, const std::filesystem::path &path) const;

    /// @brief Keep a piece of a path that did not fit in a request
    void addPathPart(pid_t tid, const char *part);

    /// @brief The path of a request, whose last piece is @p tail
    std::string takePath(pid_t tid, const char *tail);

    /// @brief Commit @p path. Never blocks.
    void commit(const std::filesystem::path &path);

    /// @brief Apply on_close to one more close of @p path. Never blocks.
    void countClose(const std::filesystem::path &path);

    /// @brief Whether a reader may have to wait for the commit of @p path, which exists
    static bool canWaitForCommit(const std::filesystem::path &path);
};

#endif // CAPIO_SERVER_FS_STREAMING_HPP

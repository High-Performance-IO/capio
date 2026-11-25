#ifndef CAPIO_SERVER_STORAGE_FS_DIRECTORY_LISTER_HPP
#define CAPIO_SERVER_STORAGE_FS_DIRECTORY_LISTER_HPP

#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/types.h>

#include "common/dirent.hpp"

/**
 * @brief Answers the getdents requests of the file system variant, on a thread of its own.
 *
 * Clients of both variants list a CAPIO directory the same way: they ask the server for the
 * entries from an offset, and get linux_dirent64 records back. In the FS variant the entries are
 * on the file system, so this thread reads the directories. Each open descriptor has its own
 * listing, which only grows, so that the offset of an entry never changes while it is listed.
 *
 * Listing a directory with an on_n_files rule may have to wait, for a new entry or for the commit.
 * Such a request stays pending, and the thread reads its directory again every 100 ms, so the
 * request thread of the server never waits for a directory.
 */
class FsDirectoryLister final {
  public:
    /// @brief How a directory is listed
    enum class Mode {
        AS_IS,           ///< No on_n_files rule, or the client produces it: list what is there now
        AS_IT_GROWS,     ///< no_update: return the entries as they appear, end at the commit
        AFTER_THE_COMMIT ///< update: wait for the commit, then return every entry
    };

    /// @brief A getdents request: @p count bytes of entries of @p path, from @p offset
    struct Request {
        pid_t tid, pid;
        int fd;
        std::string path;
        off64_t offset, count;
        Mode mode;
        long expected; ///< N of an on_n_files directory
    };

  private:
    using Key = std::pair<pid_t, int>; ///< (pid, fd)

    struct Listing {
        std::vector<linux_dirent64> entries;
        std::unordered_set<std::string> names;
        bool is_complete = false; ///< read again after the commit: no entry can be missing
    };

    // Shared with the request thread
    std::mutex _mutex;
    std::condition_variable _wake;
    std::vector<Request> _incoming;
    std::vector<Key> _forgotten;

    // Used by the thread only
    std::map<Key, Listing> _listings;
    std::vector<Request> _pending;

    void run();

    /// @brief Answer @p request if its directory allows it
    /// @return whether it was answered
    bool answer(const Request &request);

    /// @brief Add to @p listing the entries of @p path that it does not have yet
    static void read(const std::string &path, Listing &listing);

    /// @brief Whether the on_n_files directory @p path is committed, or no longer exists
    static bool isCommitted(const std::string &path, long expected);

  public:
    /// @brief Starts the thread
    FsDirectoryLister();

    FsDirectoryLister(const FsDirectoryLister &)            = delete;
    FsDirectoryLister &operator=(const FsDirectoryLister &) = delete;

    /// @brief Answer @p request, now or once its directory allows it
    void list(Request request);

    /// @brief Drop the listing of the descriptor @p fd of process @p pid, which is closed
    void forget(pid_t pid, int fd);
};

#endif // CAPIO_SERVER_STORAGE_FS_DIRECTORY_LISTER_HPP

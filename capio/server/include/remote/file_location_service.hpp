#ifndef CAPIO_SERVER_REMOTE_FILE_LOCATION_SERVICE_HPP
#define CAPIO_SERVER_REMOTE_FILE_LOCATION_SERVICE_HPP

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "calf/StlLogger.h"
#include "remote/backend.hpp"
#include "utils/types.hpp"

extern Backend *backend;

/**
 * @brief Knows which server holds each CAPIO file: its home node.
 *
 * Each server keeps a table of the files it has heard of and their home nodes. The table is kept
 * up to date over the communication backend, much like ARP keeps a table of addresses:
 *
 * - When a server creates a file, it becomes the file's home and announces it to every other
 *   server (setHome()).
 * - When it removes a file it is home of, it tells every other server to forget it (remove()).
 *   A migration, or a rename, is a removal followed by a creation.
 * - A server that does not know a file asks every other server (lookup()). The home answers
 *   that it holds the file; every other server answers that it does not, so a lookup of a file
 *   that no server holds ends as soon as every server has answered, without a timeout.
 * - A server that waits for a file to be created (waitForHome()) is woken by the announcement.
 *
 * Only running servers are asked. A file whose producer and server ended before its readers
 * started is assumed to have been written to the file system, where the readers find it.
 *
 * Lookups and waits block until the other servers answer, which the remote listener thread
 * handles: never call them from that thread.
 */
class FileLocationService {
    /// @brief Protects every member below
    mutable std::mutex _mutex;
    /// @brief Signalled when a home is learnt or a query is answered
    std::condition_variable _changed;
    /// @brief Home node of each known path
    std::unordered_map<std::string, std::string> _homes;

    /// @brief A lookup waiting for the answers of the other servers
    struct Query {
        /// @brief Answers still missing
        std::size_t awaited;
        /// @brief Home node, once a server has answered that it holds the file
        std::optional<std::string> home;
    };
    /// @brief Lookups in progress, by identifier
    std::unordered_map<long, Query> _queries;
    /// @brief Identifier of the next lookup
    long _next_query_id = 0;

    /// @brief How long a lookup waits for the other servers to answer
    std::chrono::milliseconds _query_timeout;
    /// @brief How often a wait for a file asks the other servers again, in case an announcement
    /// was sent before this server could receive it
    std::chrono::milliseconds _requery_interval;

    /// @brief Send a request about @p path to every other server; return how many were sent
    static std::size_t _broadcast(int code, const std::string &fields,
                                  const std::filesystem::path &path);

    /// @brief Send a request about @p path to the server @p target
    static void _send(int code, const std::string &fields, const std::filesystem::path &path,
                      const std::string &target);

  public:
    /**
     * @param query_timeout how long a lookup waits for the other servers to answer
     * @param requery_interval how often a wait for a file asks the other servers again
     */
    explicit FileLocationService(
        std::chrono::milliseconds query_timeout    = std::chrono::milliseconds(5000),
        std::chrono::milliseconds requery_interval = std::chrono::milliseconds(500));

    FileLocationService(const FileLocationService &)            = delete;
    FileLocationService &operator=(const FileLocationService &) = delete;

    /**
     * @brief Home node of @p path, if this server knows it. Asks no other server.
     * @param path
     * @return the home node, or nothing if this server does not know @p path
     */
    [[nodiscard]] std::optional<std::string> getHome(const std::filesystem::path &path) const;

    /**
     * @brief Home node of @p path, which this server must know. Ends the server otherwise.
     * @param path
     * @return the home node
     */
    [[nodiscard]] std::string getKnownHome(const std::filesystem::path &path) const;

    /**
     * @brief Home node of @p path, asking the other servers if this one does not know it.
     * @param path
     * @return the home node, or nothing if no running server holds @p path
     */
    std::optional<std::string> lookup(const std::filesystem::path &path);

    /**
     * @brief Wait until some server holds @p path.
     * @param path
     * @return the home node
     */
    std::string waitForHome(const std::filesystem::path &path);

    /**
     * @brief Make this server the home of @p path, and announce it to the other servers.
     * @param path
     */
    void setHome(const std::filesystem::path &path);

    /**
     * @brief Forget @p path. If this server was its home, tell the other servers to forget it.
     * @param path
     * @return 1 if this server knew @p path, 2 otherwise, as rmdir replies to its clients
     */
    int remove(const std::filesystem::path &path);

    /**
     * @brief Move this server's knowledge of @p oldpath to @p newpath, after a rename done here.
     * The other servers forget @p oldpath and learn @p newpath if this server is its home.
     * @param oldpath
     * @param newpath
     */
    void rename(const std::filesystem::path &oldpath, const std::filesystem::path &newpath);

    /**
     * @brief Record that @p home holds @p path, without telling anyone. A home this server has
     * for itself is kept: another server announcing the same path, such as a directory both
     * write into, does not take it away.
     * @param path
     * @param home
     */
    void learn(const std::filesystem::path &path, const std::string &home);

    /**
     * @brief Forget that @p home holds @p path, without telling anyone. A path whose recorded
     * home is another server is kept: it moved there after @p home removed it.
     * @param path
     * @param home
     */
    void unlearn(const std::filesystem::path &path, const std::string &home);

    /**
     * @brief Answer the lookup @p query_id of server @p source: whether this server holds
     * @p path.
     * @param query_id
     * @param path
     * @param source
     */
    void answerQuery(long query_id, const std::filesystem::path &path,
                     const std::string &source) const;

    /**
     * @brief Record the answer of server @p source to this server's lookup @p query_id.
     * @param query_id
     * @param holds whether @p source holds the file
     * @param source
     */
    void receiveAnswer(long query_id, bool holds, const std::string &source);
};

extern FileLocationService *file_location_service;

#endif // CAPIO_SERVER_REMOTE_FILE_LOCATION_SERVICE_HPP

#ifndef CAPIO_SERVER_REMOTE_HOME_NODE_SERVICE_HPP
#define CAPIO_SERVER_REMOTE_HOME_NODE_SERVICE_HPP

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
class HomeNodeService {
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

    /**
     * @brief Send the same request to every other server the backend knows.
     * @param code request code, one of the CAPIO_SERVER_REQUEST_LOCATION_* codes
     * @param fields the request's fields before the path, each followed by a space; may be empty
     * @param path the file the request is about, sent last so that it may hold spaces
     * @return how many servers the request was sent to
     */
    static std::size_t _broadcast(int code, const std::string &fields,
                                  const std::filesystem::path &path);

    /**
     * @brief Send a request to one server.
     * @param code request code, one of the CAPIO_SERVER_REQUEST_LOCATION_* codes
     * @param fields the request's fields before the path, each followed by a space; may be empty
     * @param path the file the request is about, sent last so that it may hold spaces
     * @param target the server to send it to, as the backend names it
     */
    static void _send(int code, const std::string &fields, const std::filesystem::path &path,
                      const std::string &target);

  public:
    /**
     * @brief Create a service that knows no file yet. The server passes the values of
     * RuntimeConfiguration, set with --location-query-timeout and --location-requery-interval.
     * @param query_timeout how long lookup() waits for the other servers to answer
     * @param requery_interval how often waitForHome() asks the other servers again
     */
    HomeNodeService(std::chrono::milliseconds query_timeout,
                    std::chrono::milliseconds requery_interval);

    HomeNodeService(const HomeNodeService &)            = delete;
    HomeNodeService &operator=(const HomeNodeService &) = delete;

    /**
     * @brief Home node of a file, if this server knows it. Asks no other server.
     * @param path the file
     * @return the home node, or nothing if this server does not know @p path
     */
    [[nodiscard]] std::optional<std::string> getHome(const std::filesystem::path &path) const;

    /**
     * @brief Home node of a file, asking every other server if this one does not know it, and
     * recording the answer. Blocks until all of them have answered, one of them holds the file,
     * or the query timeout passes.
     * @param path the file
     * @return the home node, or nothing if no server that answered in time holds @p path
     */
    std::optional<std::string> lookup(const std::filesystem::path &path);

    /**
     * @brief Wait until some server holds a file, however long that takes. Asks the other servers
     * with lookup(), then again every requery interval, and wakes early when the file's creation
     * is announced.
     * @param path the file
     * @return the home node
     */
    std::string waitForHome(const std::filesystem::path &path);

    /**
     * @brief Make this server the home of a file, and announce it to every other server.
     * @param path the file, just created on this server
     */
    void setHome(const std::filesystem::path &path);

    /**
     * @brief Forget a file. If this server was its home, tell every other server to forget it.
     * @param path the file, just removed
     * @return 1 if this server knew @p path, 2 otherwise, as rmdir replies to its clients
     */
    int remove(const std::filesystem::path &path);

    /**
     * @brief Update the table after a rename done on this server: remove() the old path, then
     * setHome() the new one unless this server already knows a home for it.
     * @param oldpath the file's path before the rename
     * @param newpath the file's path after it
     */
    void rename(const std::filesystem::path &oldpath, const std::filesystem::path &newpath);

    /**
     * @brief Record that a server holds a file, without telling anyone: what another server's
     * announcement does. A home this server has for itself is kept: another server announcing the
     * same path, such as a directory both write into, does not take it away.
     * @param path the file
     * @param home the server that holds it
     */
    void learn(const std::filesystem::path &path, const std::string &home);

    /**
     * @brief Forget that a server holds a file, without telling anyone: what another server's
     * removal does. A path whose recorded home is another server is kept: it moved there after
     * @p home removed it.
     * @param path the file
     * @param home the server that removed it
     */
    void forget(const std::filesystem::path &path, const std::string &home);

    /**
     * @brief Answer another server's lookup: whether this server is the home of a file.
     * @param query_id the asking server's identifier of its lookup, sent back with the answer
     * @param path the file
     * @param source the server that asked, which the answer goes to
     */
    void answerQuery(long query_id, const std::filesystem::path &path,
                     const std::string &source) const;

    /**
     * @brief Record another server's answer to one of this server's lookups. An answer to a lookup
     * that already ended, by an earlier home or the timeout, is ignored.
     * @param query_id this server's identifier of the lookup
     * @param holds whether @p source is the file's home
     * @param source the server that answered
     */
    void receiveAnswer(long query_id, bool holds, const std::string &source);
};

extern HomeNodeService *home_node_service;

#endif // CAPIO_SERVER_REMOTE_HOME_NODE_SERVICE_HPP

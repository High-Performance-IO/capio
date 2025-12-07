#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

#include <condition_variable>
#include <unordered_map>
#include <vector>

#include "common/queue.hpp"
/**
 * @brief Class to handle libcapio_posix clients applications
 */
class ClientManager {

    /**
     * Request and Response buffer variables
     */
    CircularBuffer<char> requests;
    std::unordered_map<int, CircularBuffer<off64_t>> responses;

    /**
     * Data buffers variables
     */
    struct ClientDataBuffers {
        SPSCQueue *ClientToServer;
        SPSCQueue *ServerToClient;
    };

    std::unordered_map<long, ClientDataBuffers> data_buffers;
    std::unordered_map<int, const std::string> app_names;

    /**
     * Variables to handle the init process after a SYS_clone occurs
     */
    std::mutex mutex_thread_allowed_to_continue;
    std::vector<int> thread_allowed_to_continue;
    std::condition_variable cv_thread_allowed_to_continue;

    /**
     * Files that are produced by a given pid. Used for Commit On Termination fallback rule
     */
    mutable std::unordered_map<pid_t, std::vector<std::string>> files_created_by_producer;

    /**
     * Files that are produced by a given app_name. Used to non block execution of multithreaded
     * applications with same app name when doing IO operations on files, and for
     * Commit On Termination fallback rule
     */
    mutable std::unordered_map<std::string, std::vector<std::string>> files_created_by_app_name;

  public:
    ClientManager();

    ~ClientManager();

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @param app_name
     * @param wait: wait flag is used whenever the handshake is called after a SYS_clone occurred.
     * if wait == true, then the server will spawn a thread waiting for the child tid to have its
     * data structures initialized
     * @return
     */
    void registerClient(pid_t tid, const std::string &app_name = CAPIO_DEFAULT_APP_NAME,
                        bool wait = false);

    /**
     * Unlok a child tid after the data structures have been allocated
     * @param tid
     */
    void unlockClonedChild(pid_t tid);

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    void removeClient(pid_t tid);

    /**
     * Send an offset and associate data to client identified by tid
     * @param tid
     * @param offset
     * @param buf
     * @param count
     * @return
     */
    void replyToClient(int tid, off64_t offset, char *buf, off64_t count);

    /**
     * Send an offset as a reply to a request to a connected client
     * @param tid
     * @param offset
     */
    void replyToClient(pid_t tid, off64_t offset);

    /**
     * @brief Add a file that is not yet ready to be consumed by a process to a list of files
     * waiting to be ready
     *
     * @param tid
     * @param path
     */
    void registerProducedFile(pid_t tid, std::string path);

    /**
     * Remove a file from an app name
     * @param tid
     * @param path
     */
    void removeProducedFile(pid_t tid, const std::filesystem::path &path);

    [[nodiscard]] bool isProducer(pid_t tid, const std::filesystem::path &path) const;

    /**
     * @brief Get the files that a given pid is waiting to be produced
     *
     * @param tid
     * @return auto
     */
    [[nodiscard]] const std::vector<std::string> &getProducedFiles(pid_t tid) const;

    /**
     * @brief Get the app name given a process pid
     *
     * @param tid
     * @return std::string
     */
    [[nodiscard]] const std::string &getAppName(pid_t tid) const;

    /**
     * Get the data queues associated with a give process id
     * @param tid
     * @return
     */
    [[nodiscard]] SPSCQueue &getClientToServerDataBuffers(pid_t tid);

    /**
     * Get the number of connected posix clients
     * @return
     */
    size_t getConnectedPosixClients() const;

    /**
     * Fetch next request from a connected posix client
     * @param str Allocated char buffer where the content of the request will be available
     * @return request code
     */
    int readNextRequest(char *str);
};

#endif // CLIENT_MANAGER_HPP
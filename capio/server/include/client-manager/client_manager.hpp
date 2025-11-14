#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

#include <unordered_map>
#include <vector>

#include "common/queue.hpp"
/**
 * @brief Class to handle libcapio_posix clients applications
 */
class ClientManager {
    struct ClientDataBuffers {
        SPSCQueue *ClientToServer;
        SPSCQueue *ServerToClient;
    };

    std::unordered_map<long, ClientDataBuffers> *data_buffers;
    std::unordered_map<int, const std::string> *app_names;

    /**
     * Files that are produced by a given pid. Used for Commit On Termination fallback rule
     */
    std::unordered_map<pid_t, std::vector<std::string> *> *files_created_by_producer;

    /**
     * Files that are produced by a given app_name. Used to non block execution of multithreaded
     * applications with same app name when doing IO operations on files, and for
     * Commit On Termination fallback rule
     */
    std::unordered_map<std::string, std::vector<std::string> *> *files_created_by_app_name;

  public:
    ClientManager();

    ~ClientManager();

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @param app_name
     * @return
     */
    void register_client(pid_t tid, const std::string &app_name = CAPIO_DEFAULT_APP_NAME) const;

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    void remove_client(pid_t tid) const;

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param buf
     * @param offset
     * @param count
     * @return
     */
    void reply_to_client(int tid, char *buf, off64_t offset, off64_t count) const;

    /**
     * @brief Add a file that is not yet ready to be consumed by a process to a list of files
     * waiting to be ready
     *
     * @param tid
     * @param path
     */
    void register_produced_file(pid_t tid, std::string path) const;

    /**
     * Remove a file from an app name
     * @param tid
     * @param path
     */
    void remove_produced_file(pid_t tid, const std::filesystem::path &path) const;

    [[nodiscard]] bool is_producer(pid_t tid, const std::filesystem::path &path) const;

    /**
     * @brief Get the files that a given pid is waiting to be produced
     *
     * @param tid
     * @return auto
     */
    [[nodiscard]] std::vector<std::string> *get_produced_files(pid_t tid) const;

    /**
     * @brief Get the app name given a process pid
     *
     * @param tid
     * @return std::string
     */
    [[nodiscard]] std::string get_app_name(pid_t tid) const;

    [[nodiscard]] SPSCQueue *get_client_to_server_data_buffer(pid_t tid) const;

    size_t get_connected_posix_client();
};

#endif // CLIENT_MANAGER_HPP
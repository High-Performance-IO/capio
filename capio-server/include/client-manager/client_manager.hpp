#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP
#include <capio/response_queue.hpp>
#include <unordered_map>
#include <vector>

/**
 * @brief Class to handle libcapio_posix clients applications
 */
class ClientManager {
    std::unordered_map<long, ResponseQueue *> *bufs_response;
    std::unordered_map<int, const std::string> *app_names;

    /**
     * Files that are produced by a given pid. Used for Commit On Termination fallback rule
     */
    std::unordered_map<pid_t, std::vector<std::string> *> *files_created_by_producer;

  public:
    ClientManager();

    ~ClientManager();

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @return
     */
    void register_client(const std::string &app_name, pid_t tid) const;

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    void remove_client(pid_t tid) const;

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param offset
     * @return
     */
    void reply_to_client(const pid_t tid, const capio_off64_t offset) const;

    /**
     * @brief Add a file that is not yet ready to be consumed by a process to a list of files
     * waiting to be ready
     *
     * @param tid
     * @param path
     */
    void register_produced_file(pid_t tid, std::string &path) const;

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

    size_t get_connected_posix_client();
};

#endif // CLIENT_MANAGER_HPP
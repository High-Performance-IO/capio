#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

/**
 * @brief Class to handle libcapio_posix clients applications
 */
class ClientManager {
    CSBufResponse_t *bufs_response;
    std::unordered_map<int, const std::string> *app_names;

    /**
     * Files that are produced by a given pid. Used for Commit On Termination fallback rule
     */
    std::unordered_map<pid_t, std::vector<std::string> *> *files_created_by_producer;

  public:
    ClientManager() {
        START_LOG(gettid(), "call()");
        bufs_response             = new CSBufResponse_t();
        app_names                 = new std::unordered_map<int, const std::string>;
        files_created_by_producer = new std::unordered_map<pid_t, std::vector<std::string> *>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "ClientManager initialization completed." << std::endl;
    }

    ~ClientManager() {
        START_LOG(gettid(), "call()");
        delete bufs_response;
        delete app_names;
        delete files_created_by_producer;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << "buf_response cleanup completed" << std::endl;
    }

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @return
     */
    inline void register_new_client(pid_t tid, const std::string &app_name) const {
        START_LOG(gettid(), "call(tid=%ld, app_name=%s)", tid, app_name.c_str());
        // TODO: replace numbers with constexpr
        auto *p_buf_response = new CircularBuffer<capio_off64_t>(
            SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT,
            sizeof(capio_off64_t), workflow_name);

        bufs_response->insert(std::make_pair(tid, p_buf_response));
        app_names->emplace(tid, app_name);
        files_created_by_producer->emplace(tid, new std::vector<std::string>);
    }

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    inline void remove_client(pid_t tid) const {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        auto it_resp = bufs_response->find(tid);
        if (it_resp != bufs_response->end()) {
            delete it_resp->second;
            bufs_response->erase(it_resp);
        }
        files_created_by_producer->erase(tid);
    }

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param offset
     * @return
     */
    inline void reply_to_client(pid_t tid, capio_off64_t offset) const {
        START_LOG(gettid(), "call(tid=%ld, offset=%llu)", tid, offset);
        try {
            bufs_response->at(tid)->write(&offset);
        } catch (const std::string &ex) {
            std::cout << "Unable to write: " << ex << std::endl;
        }
    }

    /**
     * @brief Add a file that is not yet ready to be consumed by a process to a list of files
     * waiting to be ready
     *
     * @param tid
     * @param path
     */
    void register_produced_file(pid_t tid, std::string &path) const {
        START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path.c_str());
        files_created_by_producer->at(tid)->emplace_back(path);
    }
    /**
     * @brief Get the files that a given pid is waiting to be produced
     *
     * @param tid
     * @return auto
     */
    [[nodiscard]] auto get_produced_files(pid_t tid) const {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        return files_created_by_producer->at(tid);
    }
    /**
     * @brief Get the app name given a process pid
     *
     * @param tid
     * @return std::string
     */
    [[nodiscard]] std::string get_app_name(pid_t tid) const {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        return app_names->at(tid);
    }
};

inline ClientManager *client_manager;

#endif // CLIENT_MANAGER_HPP

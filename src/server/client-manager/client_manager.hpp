#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

/**
 * @brief Class to handle libcapio_posix clients applications
 */
class ClientManager {
    CSBufResponse_t *bufs_response;
    std::unordered_map<int, const std::string> *app_names;

    // TODO: this is an approx. here only the creator will commit the file.
    // TODO: more complex checks needs to be done but this is a temporary fix
    std::unordered_map<pid_t, std::vector<std::string> *> *files_to_be_committed_by_tid;

  public:
    ClientManager() {
        START_LOG(gettid(), "call()");
        bufs_response                = new CSBufResponse_t();
        app_names                    = new std::unordered_map<int, const std::string>;
        files_to_be_committed_by_tid = new std::unordered_map<pid_t, std::vector<std::string> *>;
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "ClientManager initialization completed." << std::endl;
    }

    ~ClientManager() {
        START_LOG(gettid(), "call()");
        delete bufs_response;
        delete app_names;
        delete files_to_be_committed_by_tid;
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
        auto *p_buf_response =
            new CircularBuffer<capio_off64_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid),
                                              CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        bufs_response->insert(std::make_pair(tid, p_buf_response));
        app_names->emplace(tid, app_name);
        files_to_be_committed_by_tid->emplace(tid, new std::vector<std::string>);
    }

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    inline void remove_client(pid_t tid) {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        auto it_resp = bufs_response->find(tid);
        if (it_resp != bufs_response->end()) {
            delete it_resp->second;
            bufs_response->erase(it_resp);
        }
        files_to_be_committed_by_tid->erase(tid);
    }

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param offset
     * @return
     */
    inline void reply_to_client(pid_t tid, capio_off64_t offset) {
        START_LOG(gettid(), "call(tid=%ld, offset=%ld)", tid, offset);

        return bufs_response->at(tid)->write(&offset);
    }

    /**
     * @brief Add a file that is not yet ready to be consumed by a process to a list of files
     * waiting to be ready
     *
     * @param tid
     * @param path
     */
    void add_producer_file_path(pid_t tid, std::string &path) const {
        START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path.c_str());
        files_to_be_committed_by_tid->at(tid)->emplace_back(path);
    }
    /**
     * @brief Get the files that a given pid is waiting to be produced
     *
     * @param tid
     * @return auto
     */
    [[nodiscard]] auto get_produced_files(pid_t tid) const {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        return files_to_be_committed_by_tid->at(tid);
    }
    /**
     * @brief Get the app name given a process pid
     *
     * @param tid
     * @return std::string
     */
    std::string get_app_name(pid_t tid) const {
        START_LOG(gettid(), "call(tid=%ld)", tid);
        return app_names->at(tid);
    }
};

inline ClientManager *client_manager;

#endif // CLIENT_MANAGER_HPP

#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

class ClientManager {
  private:
    CSBufResponse_t *bufs_response;
    std::unordered_map<int, const std::string> *app_names;

    std::unordered_map<std::string, std::vector<pid_t> *> *thread_awaiting_file_creation;
    std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>
        *thread_awaiting_data;

    // TODO: this is an approx. here opnly the creator will commit the file.
    // TODO: more complex checks needs to be done but this is a temporary fix
    std::unordered_map<pid_t, std::vector<std::string> *> *files_to_be_committed_by_tid;

  public:
    ClientManager() {
        bufs_response                 = new CSBufResponse_t();
        app_names                     = new std::unordered_map<int, const std::string>;
        thread_awaiting_file_creation = new std::unordered_map<std::string, std::vector<pid_t> *>;
        thread_awaiting_data =
            new std::unordered_map<std::string, std::unordered_map<pid_t, capio_off64_t> *>;
        files_to_be_committed_by_tid = new std::unordered_map<pid_t, std::vector<std::string> *>;
    }

    ~ClientManager() {
        delete bufs_response;
        delete app_names;
        delete thread_awaiting_file_creation;
        delete thread_awaiting_data;
        delete files_to_be_committed_by_tid;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_response cleanup completed"
                  << std::endl;
    }

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @return
     */
    inline void register_new_client(pid_t tid, const std::string &app_name) const {
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

    void add_thread_awaiting_creation(std::string path, pid_t tid) {
        if (thread_awaiting_file_creation->find(path) == thread_awaiting_file_creation->end()) {
            thread_awaiting_file_creation->emplace(path, new std::vector<int>);
        }
        thread_awaiting_file_creation->at(path)->emplace_back(tid);
    }

    void unlock_thread_awaiting_creation(std::string path) {
        if (thread_awaiting_file_creation->find(path) != thread_awaiting_file_creation->end()) {
            auto th = thread_awaiting_file_creation->at(path);
            for (auto tid : *th) {
                reply_to_client(tid, 1);
            }
        }
    }

    // register tid to wait for file size of certain size
    void add_thread_awaiting_data(std::string path, int tid, size_t expected_size) {
        if (thread_awaiting_data->find(path) == thread_awaiting_data->end()) {
            thread_awaiting_data->emplace(path, new std::unordered_map<pid_t, capio_off64_t>);
        }
        thread_awaiting_data->at(path)->emplace(tid, expected_size);
    }

    void unlock_thread_awaiting_data(std::string path) {
        START_LOG(gettid(), "call(path=%s)", path.c_str());
        auto path_size = std::filesystem::file_size(path);

        if (thread_awaiting_data->find(path) != thread_awaiting_data->end()) {
            LOG("Path has thread awaiting");
            auto th = thread_awaiting_data->at(path);
            std::vector<pid_t> item_to_delete;

            for (auto item : *th) {
                LOG("Handling thread");
                if (CapioFileManager::is_committed(path) ||
                    item.second >= std::filesystem::file_size(path)) {
                    LOG("Thread %ld can be unlocked", item.first);
                    reply_to_client(item.first, path_size);
                    item_to_delete.emplace_back(item.first);
                }
            }
            // cleanup of served clients
            for (auto itm : item_to_delete) {
                LOG("Cleanup of thread %ld", itm);
                th->erase(itm);
            }
        }
    }

    void add_producer_file_path(pid_t tid, std::string &path) const {
        files_to_be_committed_by_tid->at(tid)->emplace_back(path);
    }

    [[nodiscard]] auto get_produced_files(pid_t tid) const {
        return files_to_be_committed_by_tid->at(tid);
    }

    auto get_file_awaiting_creation() {
        std::vector<std::string> keys;
        for (auto itm : *thread_awaiting_file_creation) {
            keys.emplace_back(itm.first);
        }
        return keys;
    }

    auto get_file_awaiting_data() {
        std::vector<std::string> keys;
        for (auto itm : *thread_awaiting_data) {
            keys.emplace_back(itm.first);
        }
        return keys;
    }
};

inline ClientManager *client_manager;

#endif // CLIENT_MANAGER_HPP

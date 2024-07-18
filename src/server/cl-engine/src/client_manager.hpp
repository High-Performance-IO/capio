#ifndef CLIENT_MANAGER_HPP
#define CLIENT_MANAGER_HPP

class ClientManager {
  private:
    CSBufResponse_t *bufs_response;
    std::unordered_map<int, const std::string> *app_names;

  public:
    ClientManager() {
        bufs_response = new CSBufResponse_t();
        app_names     = new std::unordered_map<int, const std::string>;
    }

    ~ClientManager() {
        delete bufs_response;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_response cleanup completed"
                  << std::endl;
    }

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @return
     */
    inline void register_new_client(long tid, const std::string &app_name) const {
        // TODO: replace numbers with constexpr
        auto *p_buf_response =
            new CircularBuffer<off_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid),
                                      CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        bufs_response->insert(std::make_pair(tid, p_buf_response));
        app_names->emplace(tid, app_name);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Handshaked a new client: <" << tid << ","
                  << app_name << ">" << std::endl;
    }

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    inline void remove_client(int tid) {
        auto it_resp = bufs_response->find(tid);
        if (it_resp != bufs_response->end()) {
            delete it_resp->second;
            bufs_response->erase(it_resp);
        }
    }

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param offset
     * @return
     */
    inline void reply_to_client(long tid, off64_t offset) {
        START_LOG(gettid(), "call(tid=%ld, offset=%ld)", tid, offset);

        return bufs_response->at(tid)->write(&offset);
    }
};

inline ClientManager *client_manager;

#endif // CLIENT_MANAGER_HPP

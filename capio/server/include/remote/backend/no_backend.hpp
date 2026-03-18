#ifndef CAPIO_NO_BACKEND_HPP
#define CAPIO_NO_BACKEND_HPP

class NoBackend : public Backend {

  public:
    NoBackend(int argc, char **argv) {
        START_LOG(gettid(), "call()");
        n_servers = 1;
        node_name = new char[HOST_NAME_MAX];
        gethostname(node_name, HOST_NAME_MAX);
    }

    ~NoBackend() override = default;

    const std::set<std::string> get_nodes() override { return {node_name}; }

    void handshake_servers() override {}

    RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        LOG("Halting thread execution as NoBackend was chosen");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return {nullptr, ""};
    }

    void send_file(char *shm, long int nbytes, const std::string &target) override {
        START_LOG(gettid(), "call(%.50s, %ld, %s)", shm, nbytes, target.c_str());
    }

    void send_request(const char *message, int message_len, const std::string &target) override {
        START_LOG(gettid(), "call(message=%s, message_len=%d, target=%s)", message, message_len,
                  target.c_str());
    }

    void recv_file(char *shm, const std::string &source, long int bytes_expected) override {
        START_LOG(gettid(), "call(shm=%ld, source=%s, bytes_expected=%ld)", shm, source.c_str(),
                  bytes_expected);
    }
};

#endif // CAPIO_NO_BACKEND_HPP

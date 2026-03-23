#ifndef CAPIO_DEFAULT_HPP
#define CAPIO_DEFAULT_HPP

#include "remote/backend.hpp"

class NoBackend : public Backend {

  public:
    NoBackend(int argc, char **argv) : Backend(HOST_NAME_MAX) { START_LOG(gettid(), "call()"); }

    ~NoBackend() override = default;

    void handshake_servers() override {}

    RemoteRequest read_next_request() override {
        START_LOG(gettid(), "call()");
        LOG("Halting thread execution as NoBackend was chosen");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(100));
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
#endif // CAPIO_DEFAULT_HPP

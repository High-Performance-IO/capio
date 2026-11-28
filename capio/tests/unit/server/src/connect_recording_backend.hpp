#ifndef CAPIO_SERVER_TESTS_CONNECT_RECORDING_BACKEND_HPP
#define CAPIO_SERVER_TESTS_CONNECT_RECORDING_BACKEND_HPP

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "remote/backend.hpp"

// Records the tokens the discovery service asks to connect to.
class ConnectRecordingBackend : public Backend {
    std::mutex _mutex;
    std::vector<std::string> _tokens;

  public:
    ConnectRecordingBackend() : Backend(256) {}

    void handshake_servers() override {}
    RemoteRequest read_next_request() override { return {std::string{}, std::string{}}; }
    void send_file(const char *, int, char *, long int, const std::string &) override {}
    void recv_file(char *, long int, const std::string &) override {}
    void send_request(const char *, int, const std::string &) override {}
    void connect_to(const std::string &token) override {
        const std::lock_guard lg(_mutex);
        _tokens.push_back(token);
    }

    bool connectedTo(const std::string &token) {
        const std::lock_guard lg(_mutex);
        return std::find(_tokens.begin(), _tokens.end(), token) != _tokens.end();
    }
};

#endif // CAPIO_SERVER_TESTS_CONNECT_RECORDING_BACKEND_HPP

#ifndef CAPIO_SERVER_REMOTE_BACKEND_NONE_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_NONE_HPP
#include "remote/backend.hpp"

class NoneBackend final : public Backend {
  public:
    NoneBackend(int argc, char **argv);
    ~NoneBackend() override = default;
    void handshake_servers() override;
    RemoteRequest read_next_request() override;
    void send_file(char *shm, long int nbytes, const std::string &target) override;
    void send_request(const char *message, int message_len, const std::string &target) override;
    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;
    void connect_to(const std::string &target) override;
};
#endif // CAPIO_SERVER_REMOTE_BACKEND_NONE_HPP

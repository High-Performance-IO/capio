#ifndef CAPIO_DEFAULT_HPP
#define CAPIO_DEFAULT_HPP
#include "remote/backend.hpp"

class NoBackend final : public Backend {
  public:
    NoBackend(int argc, char **argv);
    ~NoBackend() override = default;
    void handshake_servers() override;
    RemoteRequest read_next_request() override;
    void send_file(char *shm, long int nbytes, const std::string &target) override;
    void send_request(const char *message, int message_len, const std::string &target) override;
    void recv_file(char *shm, const std::string &source, long int bytes_expected) override;
};
#endif // CAPIO_DEFAULT_HPP

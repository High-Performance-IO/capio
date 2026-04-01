#include <thread>

#include "remote/backend/none.hpp"

NoneBackend::NoneBackend(int argc, char **argv) : Backend(HOST_NAME_MAX) {
    START_LOG(gettid(), "call()");
}

RemoteRequest NoneBackend::read_next_request() {
    START_LOG(gettid(), "call()");
    return {nullptr, ""};
}

void NoneBackend::send_file(char *shm, const long int nbytes, const std::string &target) {
    START_LOG(gettid(), "call(%.50s, %ld, %s)", shm, nbytes, target.c_str());
}

void NoneBackend::handshake_servers() {}

void NoneBackend::send_request(const char *message, const int message_len,
                               const std::string &target) {
    START_LOG(gettid(), "call(message=%s, message_len=%d, target=%s)", message, message_len,
              target.c_str());
}

void NoneBackend::recv_file(char *shm, const std::string &source, const long int bytes_expected) {
    START_LOG(gettid(), "call(shm=%ld, source=%s, bytes_expected=%ld)", shm, source.c_str(),
              bytes_expected);
}
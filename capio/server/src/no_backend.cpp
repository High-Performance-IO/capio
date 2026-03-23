#include <thread>

#include "remote/backend/no_backend.hpp"

NoBackend::NoBackend(int argc, char **argv) : Backend(HOST_NAME_MAX) {
    START_LOG(gettid(), "call()");
}

RemoteRequest NoBackend::read_next_request() {
    START_LOG(gettid(), "call()");
    LOG("Halting thread execution as NoBackend was chosen");
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(100));
    }
    return {nullptr, ""};
}

void NoBackend::send_file(char *shm, const long int nbytes, const std::string &target) {
    START_LOG(gettid(), "call(%.50s, %ld, %s)", shm, nbytes, target.c_str());
}

void NoBackend::handshake_servers() {}

void NoBackend::send_request(const char *message, const int message_len,
                             const std::string &target) {
    START_LOG(gettid(), "call(message=%s, message_len=%d, target=%s)", message, message_len,
              target.c_str());
}

void NoBackend::recv_file(char *shm, const std::string &source, const long int bytes_expected) {
    START_LOG(gettid(), "call(shm=%ld, source=%s, bytes_expected=%ld)", shm, source.c_str(),
              bytes_expected);
}
#ifndef BACKEND_INTERFACE_HPP
#define BACKEND_INTERFACE_HPP

#include "capio/constants.hpp"
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

class MessageQueue {

    class ResponseToRequest {
      public:
        std::string original_request;
        char *response;
        capio_off64_t response_size;

        ResponseToRequest(std::string request, char *buf, capio_off64_t buff_size)
            : original_request(std::move(request)), response(buf), response_size(buff_size) {}
    };

    std::queue<std::string> request_queue;
    std::queue<ResponseToRequest> response_queue;
    std::mutex request_queue_mutex, response_queue_mutex;

  public:
    void push_request(const std::string &request) {
        std::lock_guard lg(request_queue_mutex);
        request_queue.emplace(request);
    }

    void push_response(char *buffer, capio_off64_t buff_size, std::string origin) {
        std::lock_guard lg(response_queue_mutex);
        response_queue.emplace(std::move(origin), buffer, buff_size);
    }

    std::string get_request() {
        std::lock_guard lg(request_queue_mutex);
        std::string req = std::move(request_queue.front());
        request_queue.pop();
        return req;
    }

    std::tuple<capio_off64_t, char *> get_response() {
        timespec sleep{.tv_sec = 0, .tv_nsec = 300};
        while (!this->has_response()) {
            nanosleep(&sleep, nullptr);
        }

        std::lock_guard lg(response_queue_mutex);
        auto response = std::move(response_queue.front());
        response_queue.pop();
        return std::make_tuple(response.response_size, response.response);
    }

    bool has_requests() {
        std::lock_guard lg(request_queue_mutex);
        return !request_queue.empty();
    }

    bool has_response() {
        std::lock_guard lg(response_queue_mutex);
        return !response_queue.empty();
    }
};

class NotImplementedBackendMethod : public std::exception {
  public:
    [[nodiscard]] const char *what() const noexcept override {
        auto msg = new char[1024]{};
        sprintf(msg, "The chosen backend does not implement method: %s", __func__);
        return msg;
    }
};

class BackendInterface {
  protected:
    typedef enum { FETCH_FROM_REMOTE } BackendRequest_t;

  public:
    virtual ~BackendInterface() = default;

    /**
     * @param hostname_port who to connect to in the form of hostname:port
     */
    virtual void connect_to(std::string hostname_port) { throw NotImplementedBackendMethod(); };

    /** Fetch a chunk of CapioFile internal data from remote host
     *
     * @param hostname Hostname to request data from
     * @param filepath Path of the file targeted by the request
     * @param buffer Buffer in which data will be available
     * @param offset Offset relative to the beginning of the file from which to read from
     * @param count Size of @param buffer and hence size of the fetch operation
     * @return Amount of data returned from the remote host
     */
    virtual size_t fetchFromRemoteHost(const std::string &hostname,
                                       const std::filesystem::path &filepath, char *buffer,
                                       capio_off64_t offset, capio_off64_t count) {
        throw NotImplementedBackendMethod();
    };

    /**
     *
     * @return A vector of hostnames for which a connection exists
     */
    virtual std::vector<std::string> get_open_connections() { throw NotImplementedBackendMethod(); }
};

/*
 * This class implements a placeholder for backend interface, whenever CAPIO is only providing IO
 * coordination
 */
class NoBackend final : public BackendInterface {
  public:
    void connect_to(std::string hostname_port) override { return; };
    size_t fetchFromRemoteHost(const std::string &hostname, const std::filesystem::path &filepath,
                               char *buffer, capio_off64_t offset, capio_off64_t count) override {
        return -1;
    };

    std::vector<std::string> get_open_connections() override { return {}; }
};

inline BackendInterface *capio_backend;
#endif // BACKEND_INTERFACE_HPP

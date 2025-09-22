#ifndef BACKEND_INTERFACE_HPP
#define BACKEND_INTERFACE_HPP

#include <capio/constants.hpp>
#include <capio/logger.hpp>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
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

    std::mutex request_mutex;
    std::mutex response_mutex;

    std::condition_variable request_cv;
    std::condition_variable response_cv;

  public:
    void push_request(const std::string &request) {
        START_LOG(gettid(), "call(req=%s)", request.c_str());
        {
            LOG("Locking request_mutex");
            std::lock_guard lg(request_mutex);
            LOG("Obtained lock");
            request_queue.emplace(request);
        }
        request_cv.notify_one();
    }

    std::optional<std::string> try_get_request() {
        START_LOG(gettid(), "call()");
        LOG("Locking request_mutex");
        std::lock_guard lg(request_mutex);
        LOG("Obtained lock");
        if (request_queue.empty()) {
            return std::nullopt;
        }
        std::string req = std::move(request_queue.front());
        request_queue.pop();
        return req;
    }

    void push_response(char *buffer, capio_off64_t buff_size, std::string origin) {
        START_LOG(gettid(), "call(origin=%s, buff_size=%ld)", origin.c_str(), buff_size);
        {
            std::lock_guard lg(response_mutex);
            LOG("Obtained lock");
            response_queue.emplace(std::move(origin), buffer, buff_size);
        }
        response_cv.notify_one();
    }

    std::tuple<capio_off64_t, char *> get_response() {
        START_LOG(gettid(), "call()");
        std::unique_lock lk(response_mutex);
        response_cv.wait(lk, [this] { return !response_queue.empty(); });
        LOG("Obtained lock");
        auto response = std::move(response_queue.front());
        response_queue.pop();
        return {response.response_size, response.response};
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
    typedef enum { HAVE_FINISH_SEND_REQUEST, FETCH_FROM_REMOTE } BackendRequest_t;

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
     * @param offset Offset relative to the beginning of the file from which to read from
     * @param count Size of @param buffer and hence size of the fetch operation
     * @return Tuple of size of buffer and pointer to char* with the actual buffer
     */
    virtual std::tuple<size_t, char *> fetchFromRemoteHost(const std::string &hostname,
                                                           const std::filesystem::path &filepath,
                                                           capio_off64_t offset,
                                                           capio_off64_t count) {
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
    std::tuple<size_t, char *> fetchFromRemoteHost(const std::string &hostname,
                                                   const std::filesystem::path &filepath,
                                                   capio_off64_t offset,
                                                   capio_off64_t count) override {
        return {-1, nullptr};
    };

    std::vector<std::string> get_open_connections() override { return {}; }
};

inline BackendInterface *capio_backend;
#endif // BACKEND_INTERFACE_HPP

#ifndef CAPIO_SERVER_REMOTE_BACKEND_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_HPP
#include "capio/logger.hpp"
#include <charconv>

class RemoteRequest {
  private:
    char *_buf_recv;
    int _code;
    const std::string _source;

  public:
    RemoteRequest(char *buf_recv, const std::string &source) : _source(source) {
        START_LOG(gettid(), "call(buf_recv=%s, source=%s)", buf_recv, source.c_str());
        int code;
        auto [ptr, ec] = std::from_chars(buf_recv, buf_recv + 4, code);
        if (ec == std::errc()) {
            this->_code     = code;
            this->_buf_recv = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
            strcpy(this->_buf_recv, ptr + 1);
            LOG("Received request %d from %s : %s", this->_code, this->_source.c_str(),
                this->_buf_recv);
        } else {
            this->_code = -1;
        }
    };

    RemoteRequest(const RemoteRequest &)            = delete;
    RemoteRequest &operator=(const RemoteRequest &) = delete;

    ~RemoteRequest() { delete[] _buf_recv; }

    [[nodiscard]] auto get_source() const { return this->_source; }
    [[nodiscard]] auto get_content() const { return this->_buf_recv; }
    [[nodiscard]] auto get_code() const { return this->_code; }
};

/**
 * This class is the interface prototype
 * for capio backend communication services.
 * To implement a new backend, please implement the following
 * functions in a dedicated backend.
 */
class Backend {
  public:
    virtual ~Backend() = default;

    /**
     * Returns the node names of the CAPIO servers
     * @return A set containing the node names of all CAPIO servers
     */
    virtual const std::set<std::string> get_nodes() = 0;

    /**
     * Handshake the server applications
     */
    virtual void handshake_servers() = 0;

    /**
     * Read the next message from the incoming queue
     * @return A RemoteRequest class object containing the request contents
     */
    virtual RemoteRequest read_next_request() = 0;

    /**
     * Send file
     * @param shm buffer of data to be sent
     * @param nbytes length of @param shm
     * @param dest target to send files to
     */
    virtual void send_file(char *shm, long int nbytes, const std::string &target) = 0;

    /**
     * receive a file from another process
     * @param shm Buffer that will be filled with incoming data
     * @param source The source target to receive from
     * @param bytes_expected Size of expected incoming buffer
     */
    virtual void recv_file(char *shm, const std::string &source, long int bytes_expected) = 0;

    /**
     *
     * @param message
     * @param message_len
     * @param target
     */
    virtual void send_request(const char *message, int message_len, const std::string &target) = 0;
};

Backend *backend;

#endif // CAPIO_SERVER_REMOTE_BACKEND_HPP

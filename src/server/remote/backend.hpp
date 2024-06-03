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

enum BackendNotifyActions { writeFile, readFile };

/**
 * This class is the template prototype
 * for capio backend communication services.
 * To implement a new backend, please implement the following
 * functions in a dedicated backend.
 */
template <class T> class Backend {
  public:


    inline Backend(int argc, char *argv[]) { T::Backend(argc, argv); };

    /**
     * Returns the node names of the CAPIO servers
     * @return A set containing the node names of all CAPIO servers
     */
    inline std::set<std::string> get_nodes() { return T::get_nodes(); };

    /**
     * Handshake the server applications
     */
    inline void handshake_servers() { T::handshake_servers(); };

    /**
     * Read the next message from the incoming queue
     * @return A RemoteRequest class object containing the request contents
     */
    inline RemoteRequest read_next_request() { return T::read_next_request(); };

    /**
     * Send file
     * @param shm buffer of data to be sent
     * @param nbytes length of @param shm
     * @param dest target to send files to
     */
    inline void send_file(char *shm, long int nbytes, const std::string &target) {
        T::send_file(shm, nbytes, target);
    };

    /**
     * receive a file from another process
     * @param shm Buffer that will be filled with incoming data
     * @param source The source target to receive from
     * @param bytes_expected Size of expected incoming buffer
     */
    inline void recv_file(char *shm, const std::string &source, long int bytes_expected) {
        T::recv_file(shm, source, bytes_expected);
    };

    /**
     *
     * @param message
     * @param message_len
     * @param target
     */
    inline void send_request(const char *message, int message_len, const std::string &target) {
        T::send_request(message, message_len, target);
    };

    /**
     * This method is used to notify the backend that a local action has occurred
     *
     * @param actions The action that has occurred
     * @param buffer The buffer to which action applies
     * @param buffer_size The size of @param buffer
     * @return 0 if nothing happens, or the method is not implemented, 1 if the
     * action has been registered, -1 on error
     */
    inline void notify_backend(enum BackendNotifyActions actions, const std::filesystem::path &file_path,
                               void *buffer, size_t offset, size_t buffer_size) {
        T::notify_backend(actions, file_path, buffer, buffer_size);
    };

    /**
     * Let CAPIO server know if backend stores the files inside the memory of a nod or not
     * @return
     */
    inline bool store_file_in_memory() { return T::store_file_in_memory(); };
};


template <class T> Backend<T> *backend;

#endif // CAPIO_SERVER_REMOTE_BACKEND_HPP

#ifndef CAPIO_SERVER_REMOTE_BACKEND_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_HPP
#include <charconv>
#include <set>

#include "common/logger.hpp"

class RemoteRequest {
    char *_buf_recv;
    int _code;
    const std::string _source;

  public:
    /**
     * Instantiate a new RemoteRequest
     * @param buf_recv The buffer containing the raw request
     * @param source The source that generated the request
     */
    RemoteRequest(char *buf_recv, const std::string &source);

    RemoteRequest(const RemoteRequest &)            = delete;
    RemoteRequest &operator=(const RemoteRequest &) = delete;

    ~RemoteRequest() { delete[] _buf_recv; }

    /// Get the source node name of the request
    [[nodiscard]] const std::string &get_source() const;
    /// Get the content of the request
    [[nodiscard]] const char *get_content() const;
    /// Get the request code
    [[nodiscard]] int get_code() const;
};

/**
 * This class is the interface prototype
 * for capio backend communication services.
 * To implement a new backend, please implement the following
 * functions in a dedicated backend.
 */
class Backend {

  protected:
    int n_servers;
    std::string node_name;

  public:
    explicit Backend(unsigned int node_name_max_length);

    virtual ~Backend() = default;

    /// Return THIS node name as configured by the derived backend class
    [[nodiscard]] const std::string &get_node_name() const;

    /// Get a std::set containing the node names of all CAPIO servers for which a handshake
    /// occurred (including current instance node name)
    virtual std::set<std::string> get_nodes();

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

// FIXME: Remove the inline specifier
inline Backend *backend;

#endif // CAPIO_SERVER_REMOTE_BACKEND_HPP

#ifndef CAPIO_SERVER_REMOTE_BACKEND_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_HPP
#include <charconv>
#include <set>
#include <string>

class RemoteRequest {
    std::string _content;
    int _code = -1;
    std::string _source;

  public:
    /**
     * Instantiate a new RemoteRequest
     * @param buf_recv The buffer containing the raw request
     * @param source The source that generated the request
     */
    RemoteRequest(std::string buf_recv, std::string source);
    RemoteRequest(char *buf_recv, const std::string &source);

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
    virtual const std::set<std::string> get_nodes();

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
     * Send a request followed by its file payload as one non-interleavable transfer.
     *
     * The request identifies and describes the payload. Backends may encode both parts in one
     * transport message or serialize multiple transport messages, but no other request may be
     * inserted between them.
     *
     * @param message Serialized request associated with the file payload
     * @param message_len Number of valid bytes in @p message
     * @param shm Buffer containing the file payload
     * @param nbytes Number of payload bytes to send from @p shm
     * @param target Destination server identifier
     */
    virtual void send_file(const char *message, int message_len, char *shm, long int nbytes,
                           const std::string &target) = 0;

    /**
     * Receive the file payload associated with the current request.
     *
     * The call blocks until the expected payload is available or the backend reports an error.
     * The caller must provide a writable buffer of at least @p bytes_expected bytes.
     *
     * @param shm Destination buffer for the received payload
     * @param bytes_expected Exact number of payload bytes expected
     * @param source Identifier of the server that sent the current request
     */
    virtual void recv_file(char *shm, long int bytes_expected, const std::string &source) = 0;

    /**
     *
     * @param message
     * @param message_len
     * @param target
     */
    virtual void send_request(const char *message, int message_len, const std::string &target) = 0;

    /**
     * Connect this server instance to a remote server instance
     * @param target Remote server instance identification
     */
    virtual void connect_to(const std::string &target) = 0;
};

#endif // CAPIO_SERVER_REMOTE_BACKEND_HPP

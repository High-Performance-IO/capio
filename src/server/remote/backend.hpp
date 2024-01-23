#ifndef CAPIO_SERVER_REMOTE_BACKEND_HPP
#define CAPIO_SERVER_REMOTE_BACKEND_HPP

class RemoteRequest {
  private:
    char *_buf_recv;
    int _code;
    int _source;

  public:
    RemoteRequest(char *buf_recv, int source) : _source(source) {
        START_LOG(gettid(), "call(buf_recv=%s, source=%d)", buf_recv, source);
        int code;
        auto [ptr, ec] = std::from_chars(buf_recv, buf_recv + 4, code);
        if (ec == std::errc()) {
            this->_code     = code;
            this->_buf_recv = new char[CAPIO_SERVER_REQUEST_MAX_SIZE];
            strcpy(this->_buf_recv, ptr + 1);
            LOG("Received request %d from %d : %s", this->_code, this->_source, this->_buf_recv);
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
    sem_t remote_read_sem{};

    /**
     * This function parses argv and sets up required elements for the communication library
     * It also sets up the node rank.
     * It also allocates the node_name variable, and fills it with the node name.
     * @param argc program argc parameter
     * @param argv program argv argument
     * @param rank A ptr to integer variable to store the rank
     * @param provided A ptr to integer variable that tells whether multithreading is available
     */
    virtual void initialize(int argc, char **argv, int *rank, int *provided) = 0;

    /**
     * Gracefully terminates the communication backend service
     */
    virtual void destroy() = 0;

    /**
     * Handshake the server applications
     * @param rank Rank of the invoker thread
     */
    virtual void handshake_servers(int rank) = 0;

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
    virtual void send_file(char *shm, long int nbytes, int dest) = 0;

    /**
     * Sends a batch of files to another node
     * @param prefix
     * @param files_to_send An array of file names to be sent
     * @param nfiles The count of files to be sent
     * @param dest The target destination
     */
    virtual void send_files_batch(const std::string &prefix,
                                  std::vector<std::string> *files_to_send, int nfiles,
                                  int dest) = 0;

    /**
     *
     * @param path_c
     * @param dest
     * @param offset
     * @param nbytes
     * @param complete
     */
    virtual void serve_remote_read(const std::filesystem::path &path, int dest, long int offset,
                                   long int nbytes, int complete) = 0;

    /**
     * Handle a remote read request
     * @param path
     * @param offset
     * @param count
     * @param rank
     */
    virtual void handle_remote_read(const std::filesystem::path &path, off64_t offset,
                                    off64_t count, int rank) = 0;

    /**
     * Handle several remote read requests
     * @param path
     * @param app_name
     * @param dest
     * @return
     */
    virtual bool handle_nreads(const std::filesystem::path &path, const std::string &app_name,
                               int dest) = 0;

    /**
     * Handle a remote stat
     * @param path Pathname of stat to be carried on
     * @param dest Target to send the stats of @param path to
     * @param c_file the capio file on which the stat is carried out
     */
    virtual void serve_remote_stat(const std::filesystem::path &path, int dest,
                                   const CapioFile &c_file) = 0;

    /**
     * Handle a remote stat request
     * @param tid
     * @param path
     * @param rank
     */
    virtual void handle_remote_stat(int tid, const std::filesystem::path &path, int rank) = 0;

    /**
     * receive a file from another process
     * @param shm Buffer that will be filled with incoming data
     * @param source The source target to receive from
     * @param bytes_expected Size of expected incoming buffer
     */
    virtual void recv_file(char *shm, int source, long int bytes_expected) = 0;
};

Backend *backend;

#endif // CAPIO_SERVER_REMOTE_BACKEND_HPP

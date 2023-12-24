#ifndef CAPIO_INTERFACES_HPP
#define CAPIO_INTERFACES_HPP

class RemoteRequest {
  private:
    char *_buf_recv;
    int _request_code;
    int _source;

    auto static read_next_request_code(char *req) {
        int code;
        auto [ptr, ec] = std::from_chars(req, req + 4, code);
        if (ec == std::errc()) {
            strcpy(req, ptr + 1);
            return code;
        } else {
            return -1;
        }
    }

  public:
    RemoteRequest(char *buf_recv, int source) : _buf_recv(buf_recv), _source(source) {
        this->_request_code = RemoteRequest::read_next_request_code(buf_recv);
    };
    ~RemoteRequest() { delete[] _buf_recv; }

    [[nodiscard]] auto getSource() const { return this->_source; }
    auto getRequest() { return this->_buf_recv; }
    [[nodiscard]] auto getRequestCode() const { return this->_request_code; }
};

typedef void (*CComsHandler_t)(RemoteRequest *, void *, void *);

/**
 * This class is the interface prototype
 * for capio backend communication services.
 * To implement a new backend, please implement the following
 * functions in a dedicated backend.
 */
class backend_interface {
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
     * @param sems
     */
    virtual void destroy(std::vector<sem_t *> *sems) = 0;

    /**
     * Handshake the server applications
     * @param rank Rank of the invoker thread
     */
    virtual void handshake_servers(int rank) = 0;

    /**
     * Read the next message from the incoming queue
     * @return A RemoteRequest class object containing the request contents
     */
    virtual RemoteRequest *read_next_request() = 0;

    /**
     * Send file
     * @param shm buffer of data to be sent
     * @param nbytes length of @param shm
     * @param dest target to send files to
     */
    virtual void send_file(char *shm, long int nbytes, int dest) = 0;

    /**
     * Sends a bunch of files to another node
     * @param prefix
     * @param files_to_send An array of file names to be sent
     * @param n_files The count of files to be sent
     * @param dest The target destination
     */
    virtual void send_n_files(const std::string &prefix, std::vector<std::string> *files_to_send,
                              int n_files, int dest) = 0;

    /**
     *
     * @param path_c
     * @param dest
     * @param offset
     * @param nbytes
     * @param complete
     */
    virtual void serve_remote_read(const char *path_c, int dest, long int offset, long int nbytes,
                                   int complete) = 0;

    /**
     * Handle a remote read request
     * @param tid
     * @param fd
     * @param count
     * @param rank
     * @param dir
     * @param is_getdents
     * @param pending_remote_reads
     * @param pending_remote_reads_mutex
     * @param handle_local_read
     */
    virtual void
    handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir, bool is_getdents,
                       CSMyRemotePendingReads_t *pending_remote_reads,
                       std::mutex *pending_remote_reads_mutex,
                       void (*handle_local_read)(int, int, off64_t, bool, bool, bool)) = 0;

    /**
     * Handle several remote read requests
     * @param path
     * @param app_name
     * @param dest
     * @return
     */
    virtual bool handle_nreads(const std::string &path, const std::string &app_name, int dest) = 0;

    /**
     * Handle a remote stat
     * @param path Pathname of stat to be carried on
     * @param dest Target to send the stats of @param path to
     * @param c_file the capio file on which the stat is carried out
     */
    virtual void serve_remote_stat(const char *path, int dest, const Capio_file &c_file) = 0;

    /**
     * Handle a remote stat request
     * @param tid
     * @param path
     * @param rank
     * @param pending_remote_stats
     * @param pending_remote_stats_mutex
     */
    virtual void handle_remote_stat(int tid, const std::string &path, int rank,
                                    CSMyRemotePendingStats_t *pending_remote_stats,
                                    std::mutex *pending_remote_stats_mutex) = 0;

    /**
     * receive a file from another process
     * @param shm Buffer that will be filled with incoming data
     * @param source The source target to receive from
     * @param bytes_expected Size of expected incoming buffer
     */
    virtual void recv_file(char *shm, int source, long int bytes_expected) = 0;
};

/**
    THE FOLLOWING FUNCTIONS PROTOTYPES ARE HERE TO ALLOW FOR DEFINITION AND INCLUSION IN DIFFERENT
   FILES
 */

inline bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
                                off64_t end_of_sector, off64_t count, const std::string &path);

inline void solve_remote_reads(size_t bytes_received, size_t offset, size_t file_size,
                               const char *path_c, bool complete,
                               CSMyRemotePendingReads_t *pending_remote_reads,
                               std::mutex *pending_remote_reads_mutex);

void wait_for_n_files(char *const prefix, std::vector<std::string> *files_path, size_t n_files,
                      int dest, sem_t *sem);

inline void wait_for_file(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank,
                          CSMyRemotePendingReads_t *pending_remote_reads,
                          std::mutex *pending_remote_reads_mutex,
                          void (*handle_local_read)(int, int, off64_t, bool, bool, bool));

#endif // CAPIO_INTERFACES_HPP

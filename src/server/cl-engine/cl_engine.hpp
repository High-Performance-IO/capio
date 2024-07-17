#ifndef CAPIO_CL_ENGINE_MAIN_HPP
#define CAPIO_CL_ENGINE_MAIN_HPP
#include "capio/requests.hpp"
#include "src/capio_file_locations.hpp"
#include "src/json_parser.hpp"

class ClEngine {
  private:
    //// Variables
    CapioFileLocations *locations;
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers;

    CSBufRequest_t *buf_requests;
    CSBufResponse_t *bufs_response;

    //// Class methods

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
        std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

        _request_handlers[CAPIO_REQUEST_ACCESS]              = nullptr;
        _request_handlers[CAPIO_REQUEST_CLONE]               = nullptr;
        _request_handlers[CAPIO_REQUEST_CLOSE]               = nullptr;
        _request_handlers[CAPIO_REQUEST_CREATE]              = nullptr;
        _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = nullptr;
        _request_handlers[CAPIO_REQUEST_GETDENTS]            = nullptr;
        _request_handlers[CAPIO_REQUEST_GETDENTS64]          = nullptr;
        _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = nullptr;
        _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = nullptr;
        _request_handlers[CAPIO_REQUEST_MKDIR]               = nullptr;
        _request_handlers[CAPIO_REQUEST_OPEN]                = nullptr;
        _request_handlers[CAPIO_REQUEST_READ]                = nullptr;
        _request_handlers[CAPIO_REQUEST_RENAME]              = nullptr;
        _request_handlers[CAPIO_REQUEST_RMDIR]               = nullptr;
        _request_handlers[CAPIO_REQUEST_SEEK]                = nullptr;
        _request_handlers[CAPIO_REQUEST_SEEK]                = nullptr;
        _request_handlers[CAPIO_REQUEST_STAT]                = nullptr;
        _request_handlers[CAPIO_REQUEST_UNLINK]              = nullptr;
        _request_handlers[CAPIO_REQUEST_WRITE]               = nullptr;

        return _request_handlers;
    }

    /**
     * Read next incoming request into @param str and returns the request code
     * @param str
     * @return request code
     */
    inline auto read_next_request(char *str) {
        char req[CAPIO_REQ_MAX_SIZE];
        buf_requests->read(req);
        START_LOG(gettid(), "call(req=%s)", req);
        int code       = -1;
        auto [ptr, ec] = std::from_chars(req, req + 4, code);
        if (ec == std::errc()) {
            strcpy(str, ptr + 1);
        } else {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                      << std::endl;
            ERR_EXIT("Invalid request %d%s", code, ptr);
        }
        return code;
    }

  public:
    explicit ClEngine(const std::filesystem::path &json_path) {
        START_LOG(gettid(), "call(path=%s)", json_path.c_str());

        locations        = JsonParser::parse(json_path);
        request_handlers = build_request_handlers_table();

        buf_requests  = new CSBufRequest_t(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT,
                                           CAPIO_REQ_MAX_SIZE, workflow_name);
        bufs_response = new CSBufResponse_t();
        locations->print();

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                  << " CL-Engine initialization completed. ready to listen for incoming requests" << std::endl;
    }

    ~ClEngine() {
        delete buf_requests;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_requests cleanup completed"
                  << std::endl;

        delete bufs_response;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_response cleanup completed"
                  << std::endl;
    }

    /**
     * Add a new response buffer for thread @param tid
     * @param tid
     * @return
     */
    inline void register_new_client(long tid) const {
        // TODO: replace numbers with constexpr
        auto *p_buf_response =
            new CircularBuffer<off_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid),
                                      CAPIO_REQ_BUFF_CNT, sizeof(off_t), workflow_name);
        bufs_response->insert(std::make_pair(tid, p_buf_response));
    }

    /**
     * Delete the response buffer associated with thread @param tid
     * @param tid
     * @return
     */
    inline void remove_client(int tid) {
        auto it_resp = bufs_response->find(tid);
        if (it_resp != bufs_response->end()) {
            delete it_resp->second;
            bufs_response->erase(it_resp);
        }
    }

    /**
     * Write offset to response buffer of process @param tid
     * @param tid
     * @param offset
     * @return
     */
    inline void reply_to_client(int tid, off64_t offset) {
        START_LOG(gettid(), "call(tid=%d, offset=%ld)", tid, offset);

        return bufs_response->at(tid)->write(&offset);
    }

    [[noreturn]] void start() {
        START_LOG(gettid(), "call()");

        auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
        while (true) {
            LOG(CAPIO_LOG_SERVER_REQUEST_START);
            int code = read_next_request(str.get());
            if (code < 0 || code > CAPIO_NR_REQUESTS) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                          << std::endl;

                ERR_EXIT("Error: received invalid request code");
            }
            request_handlers[code](str.get());
            LOG(CAPIO_LOG_SERVER_REQUEST_END);
        }
    }
};

inline ClEngine *cl_engine;

#endif // CAPIO_CL_ENGINE_MAIN_HPP

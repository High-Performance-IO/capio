#ifndef CAPIO_CL_ENGINE_MAIN_HPP
#define CAPIO_CL_ENGINE_MAIN_HPP
#include "capio/requests.hpp"
#include "src/capio_cl_configuration.hpp"
#include "src/client_manager.hpp"
#include "src/json_parser.hpp"

#include "src/handlers.hpp"

class ClEngine {
  private:
    //// Variables
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers;

    CSBufRequest_t *buf_requests;

    //// Class methods

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
        std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

        _request_handlers[CAPIO_REQUEST_CONSENT]             = consent_to_proceed_handler;
        _request_handlers[CAPIO_REQUEST_CLOSE]               = close_handler;
        _request_handlers[CAPIO_REQUEST_CREATE]              = create_handler;
        _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = exit_handler;
        _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = handshake_named_handler;
        _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = handshake_anonymous_handler;
        _request_handlers[CAPIO_REQUEST_MKDIR]               = create_handler;
        _request_handlers[CAPIO_REQUEST_OPEN]                = open_handler;
        _request_handlers[CAPIO_REQUEST_READ]                = read_handler;
        _request_handlers[CAPIO_REQUEST_RENAME]              = rename_handler;
        _request_handlers[CAPIO_REQUEST_SEEK]                = seek_handler;
        _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;

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

        client_manager      = new ClientManager();
        capio_configuration = JsonParser::parse(json_path);
        request_handlers    = build_request_handlers_table();

        buf_requests = new CSBufRequest_t(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT,
                                          CAPIO_REQ_MAX_SIZE, workflow_name);

        capio_configuration->print();

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                  << " CL-Engine initialization completed. ready to listen for incoming requests"
                  << std::endl;
    }

    ~ClEngine() {
        delete buf_requests;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "buf_requests cleanup completed"
                  << std::endl;
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

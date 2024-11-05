#ifndef CAPIO_CL_ENGINE_MAIN_HPP
#define CAPIO_CL_ENGINE_MAIN_HPP

#include "capio-cl-engine/capio_cl_engine.hpp"
#include "capio-cl-engine/json_parser.hpp"
#include "capio/requests.hpp"
#include "client_manager.hpp"
#include "file-manager/file_manager.hpp"

/*
 * REQUESTS handlers
 */
#include "handlers/close.hpp"
#include "handlers/consent.hpp"
#include "handlers/create.hpp"
#include "handlers/exit.hpp"
#include "handlers/handshake.hpp"
#include "handlers/open.hpp"
#include "handlers/read.hpp"
#include "handlers/rename.hpp"
#include "handlers/write.hpp"

/**
 * @brief Class that handles the system calls received from the posix client application
 *
 */
class RequestHandlerEngine {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers{};
    CSBufRequest_t *buf_requests;

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
        std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

        _request_handlers[CAPIO_REQUEST_CONSENT]    = consent_to_proceed_handler;
        _request_handlers[CAPIO_REQUEST_CLOSE]      = close_handler;
        _request_handlers[CAPIO_REQUEST_CREATE]     = create_handler;
        _request_handlers[CAPIO_REQUEST_EXIT_GROUP] = exit_handler;
        _request_handlers[CAPIO_REQUEST_HANDSHAKE]  = handshake_handler;
        _request_handlers[CAPIO_REQUEST_MKDIR]      = create_handler;
        _request_handlers[CAPIO_REQUEST_OPEN]       = open_handler;
        _request_handlers[CAPIO_REQUEST_READ]       = read_handler;
        _request_handlers[CAPIO_REQUEST_RENAME]     = rename_handler;
        _request_handlers[CAPIO_REQUEST_WRITE]      = write_handler;

        return _request_handlers;
    }

    /**
     * Read next incoming request into @param str and returns the request code
     * @param str
     * @return request code
     */
    inline auto read_next_request(char *str) const {
        char req[CAPIO_REQ_MAX_SIZE];
        buf_requests->read(req);
        START_LOG(gettid(), "call(req=%s)", req);
        int code       = -1;
        auto [ptr, ec] = std::from_chars(req, req + 4, code);
        if (ec == std::errc()) {
            strcpy(str, ptr + 1);
        } else {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                      << "Received invalid code: " << code << std::endl;
            ERR_EXIT("Invalid request %d%s", code, ptr);
        }
        return code;
    }

  public:
    explicit RequestHandlerEngine() {
        START_LOG(gettid(), "call()");

        client_manager   = new ClientManager();
        request_handlers = build_request_handlers_table();
        buf_requests     = new CSBufRequest_t(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT,
                                              CAPIO_REQ_MAX_SIZE, workflow_name);

        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "RequestHandlerEngine initialization completed." << std::endl;
    }

    ~RequestHandlerEngine() {
        START_LOG(gettid(), "call()");
        delete buf_requests;

        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
                  << "buf_requests cleanup completed" << std::endl;
    }

    /**
     * @brief Start the main loop on the main thread that will read each request one by one from all
     * the posix clients (aggregated) and handle the response
     *
     */
    [[noreturn]] void start() const {
        START_LOG(gettid(), "call()\n\n");

        auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
        while (true) {
            LOG(CAPIO_LOG_SERVER_REQUEST_START);
            int code = read_next_request(str.get());
            if (code < 0 || code > CAPIO_NR_REQUESTS) {
                std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                          << "Received invalid code: " << code << std::endl;

                ERR_EXIT("Error: received invalid request code");
            }
            request_handlers[code](str.get());
            LOG(CAPIO_LOG_SERVER_REQUEST_END);
        }
    }
};

inline RequestHandlerEngine *request_handlers_engine;

#endif // CAPIO_CL_ENGINE_MAIN_HPP

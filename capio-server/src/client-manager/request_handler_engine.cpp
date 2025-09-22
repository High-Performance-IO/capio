#include <csignal>
#include <include/client-manager/client_manager.hpp>
#include <include/client-manager/handlers.hpp>
#include <include/client-manager/request_handler_engine.hpp>

constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS>
RequestHandlerEngine::build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_CONSENT]             = consent_to_proceed_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]               = close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]              = create_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = exit_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE]           = handshake_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]               = create_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]                = open_handler;
    _request_handlers[CAPIO_REQUEST_READ]                = read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]              = rename_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;
    _request_handlers[CAPIO_REQUEST_QUERY_MEM_FILE]      = files_to_store_in_memory_handler;
    _request_handlers[CAPIO_REQUEST_READ_MEM]            = read_mem_handler;
    _request_handlers[CAPIO_REQUEST_WRITE_MEM]           = write_mem_handler;
    _request_handlers[CAPIO_REQUEST_POSIX_DIR_COMMITTED] = posix_readdir_handler;

    return _request_handlers;
}

auto RequestHandlerEngine::read_next_request(char *str) const {
    char req[CAPIO_REQ_MAX_SIZE];
    START_LOG(gettid(), "call()");
    buf_requests->read(req);
    LOG("req=%s", req);
    int code       = -1;
    auto [ptr, ec] = std::from_chars(req, req + 4, code);
    if (ec == std::errc()) {
        strcpy(str, ptr + 1);
    } else {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Received invalid code: " + std::to_string(code));
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "Offending request: " + std::string(ptr) + " / " + req);
        ERR_EXIT("Invalid request %d:%s", code, ptr);
    }
    return code;
}

RequestHandlerEngine::RequestHandlerEngine() {
    START_LOG(gettid(), "call()");
    request_handlers = build_request_handlers_table();
    buf_requests = new CSBufRequest_t(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE,
                                      capio_global_configuration->workflow_name);

    server_println(CAPIO_SERVER_CLI_LOG_SERVER, "RequestHandlerEngine initialization completed.");
}

RequestHandlerEngine::~RequestHandlerEngine() {
    START_LOG(gettid(), "call()");
    delete buf_requests;

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "RequestHandlerEngine cleanup completed.");
}

void RequestHandlerEngine::start() const {
    START_LOG(gettid(), "call()\n\n");

    const auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
    int code;

    /* When in termination_phase, we empty all requests while clients are connected. as soon
     * as queues are empty and the server ha removed all requests, it calls the termination
     * handler to stop the server execution
     */
    while (!capio_global_configuration->termination_phase ||
           client_manager->get_connected_posix_client() > 0) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        try {
            code = read_next_request(str.get());
        } catch (const std::exception &e) {
            if (capio_global_configuration->termination_phase) {
                server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                               "Termination phase is in progress... "
                               "Ignoring Exception likely  thrown while receiving SIGUSR1");
                continue;
            }
            throw;
        }

        try {
            request_handlers[code](str.get());
        } catch (const std::exception &exception) {
            std::cout << std::endl
                      << "~~~~~~~~~~~~~~[\033[31mRequestHandlerEngine::start(): FATAL "
                         "EXCEPTION\033[0m]~~~~~~~~~~~~~~"
                      << std::endl
                      << "|  Exception thrown while handling request number: " << code << " : "
                      << str.get() << std::endl
                      << "|  TID of offending thread: " << gettid() << std::endl
                      << "|  PID of offending thread: " << getpid() << std::endl
                      << "|  PPID of offending thread: " << getppid() << std::endl
                      << "|  " << std::endl
                      << "|  `" << typeid(exception).name() << ": " << exception.what() << std::endl
                      << "|" << std::endl
                      << "~~~~~~~~~~~~~~[\033[31mRequestHandlerEngine::start(): FATAL "
                         "EXCEPTION\033[0m]~~~~~~~~~~~~~~"
                      << std::endl
                      << std::endl;

            capio_global_configuration->termination_phase = true;
            kill(getpid(), SIGUSR1);
        }

        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }

    LOG("Terminated handling of posix clients");
}
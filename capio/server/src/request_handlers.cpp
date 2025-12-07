
#include "client-manager/request_handlers.hpp"
#include "client-manager/handlers.hpp"
#include "utils/common.hpp"

constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS>
RequestHandler::build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_ACCESS]              = access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]               = clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]               = close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]              = create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE]    = create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]                 = dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]               = fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]            = getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]          = getdents_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = handshake_anonymous_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]               = mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]                = open_handler;
    _request_handlers[CAPIO_REQUEST_READ]                = read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]              = rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]               = rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]                = lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]           = seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]            = seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]           = seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]                = stat_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]              = unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;

    return _request_handlers;
}

RequestHandler::RequestHandler() {
    request_handlers = build_request_handlers_table();
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "RequestHandler initialization completed.");
}
RequestHandler::~RequestHandler() {
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "RequestHandler teardown completed.");
}

void RequestHandler::start() const {
    START_LOG(gettid(), "call()");
    auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
    while (true) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        int code = client_manager->readNextRequest(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                      << std::endl;

            ERR_EXIT("Error: received invalid request code");
        }
        request_handlers[code](str.get());
        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }
}

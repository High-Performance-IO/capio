#include "client/manager.hpp"
#include "client/request.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"
#include "utils/signals.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

constexpr std::array<ClientRequestManager::CSHandler_t, CAPIO_NR_REQUESTS>
ClientRequestManager::build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_ACCESS]              = ClientHandlers::access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]               = ClientHandlers::clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]               = ClientHandlers::close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]              = ClientHandlers::create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE]    = ClientHandlers::create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]                 = ClientHandlers::dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = ClientHandlers::exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]               = ClientHandlers::fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]            = ClientHandlers::getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]          = ClientHandlers::getdents_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = ClientHandlers::handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = ClientHandlers::handshake_anonymous_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]               = ClientHandlers::mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]                = ClientHandlers::open_handler;
    _request_handlers[CAPIO_REQUEST_READ]                = ClientHandlers::read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]              = ClientHandlers::rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]               = ClientHandlers::rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]                = ClientHandlers::lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]           = ClientHandlers::seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]            = ClientHandlers::seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]           = ClientHandlers::seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]                = ClientHandlers::stat_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]              = ClientHandlers::unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = ClientHandlers::write_handler;

    return _request_handlers;
}

ClientRequestManager::ClientRequestManager() : request_handlers(build_request_handlers_table()) {
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "ClientRequestManager initialized correctly.");
}

void ClientRequestManager::start() const {

    START_LOG(gettid(), "call()");

    const auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
    while (true) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        const int code = client_manager->readNextRequest(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Received invalid code: " << code
                      << std::endl;

            ERR_EXIT("Error: received invalid request code");
        }
        request_handlers[code](str.get());
        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }
}
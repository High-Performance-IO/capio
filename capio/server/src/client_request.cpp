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

    _request_handlers[CAPIO_REQUEST_ACCESS]           = MemHandlers::access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]            = MemHandlers::clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]            = MemHandlers::close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]           = MemHandlers::create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE] = MemHandlers::create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]              = MemHandlers::dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]       = Handlers::exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]            = MemHandlers::fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]         = MemHandlers::getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]       = MemHandlers::getdents_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]  = Handlers::handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]            = MemHandlers::mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]             = MemHandlers::open_handler;
    _request_handlers[CAPIO_REQUEST_READ]             = MemHandlers::read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]           = MemHandlers::rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]            = MemHandlers::rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]             = MemHandlers::lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]        = MemHandlers::seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]         = MemHandlers::seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]        = MemHandlers::seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]             = MemHandlers::stat_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]           = MemHandlers::unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]            = MemHandlers::write_handler;

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
#include "client-manager/request_manager.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>

#include <unistd.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "common/requests.hpp"
#include "handlers.hpp"
#include "utils/server_state.hpp"

namespace {

constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_ACCESS]           = access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]            = clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]            = close_handler;
    _request_handlers[CAPIO_REQUEST_COPY_FILE_RANGE]  = copy_file_range_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]           = create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE] = create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]              = dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]       = exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]            = fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]         = getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]       = getdents_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]  = handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]            = mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]             = open_handler;
    _request_handlers[CAPIO_REQUEST_READ]             = read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]           = rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]            = rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]             = lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]        = seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]         = seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]        = seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]             = stat_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]           = unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]            = write_handler;
    _request_handlers[CAPIO_REQUEST_CONSENT]          = consent_handler;
    _request_handlers[CAPIO_REQUEST_READ_FS]          = read_fs_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_FS]        = create_fs_handler;
    _request_handlers[CAPIO_REQUEST_OPEN_FS]          = open_fs_handler;
    _request_handlers[CAPIO_REQUEST_RENAME_FS]        = rename_fs_handler;
    _request_handlers[CAPIO_REQUEST_PATH_PART]        = path_part_handler;

    return _request_handlers;
}

constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers =
    build_request_handlers_table();

/// @brief Held while a request is handled, and for good once the server stops (see stop())
std::timed_mutex handling;
/// @brief The thread handling a request, 0 if none
std::atomic<pid_t> handler_thread{0};

} // namespace

CSHandler_t ClientRequestManager::handlerOf(const int code) {
    return code < 0 || code >= CAPIO_NR_REQUESTS ? nullptr : request_handlers[code];
}

bool ClientRequestManager::handle(const int code, const char *const request) {
    START_LOG(gettid(), "call(code=%d)", code);
    const CSHandler_t handler = handlerOf(code);
    if (handler == nullptr) {
        return false;
    }
    const std::lock_guard lock(handling);
    handler_thread = gettid();
    handler(request);
    handler_thread = 0;
    return true;
}

bool ClientRequestManager::stop(const std::chrono::milliseconds timeout) {
    if (handler_thread == gettid()) {
        return false; // a fault in a handler: it cannot wait for itself
    }
    // Never unlocked: the server exits
    return handling.try_lock_for(timeout);
}

void ClientRequestManager::start() {
    const auto request = std::make_unique<char[]>(CAPIO_REQ_MAX_SIZE);
    while (true) {
        START_LOG(gettid(), "call()");
        const int code = client_manager->readNextRequest(request.get());
        if (!handle(code, request.get())) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Received invalid code: %d", code);
            ERR_EXIT("Error: received invalid request code");
        }
    }
}

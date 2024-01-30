#ifndef CAPIO_SERVER_REMOTE_LISTENER_HPP
#define CAPIO_SERVER_REMOTE_LISTENER_HPP

#include "capio/logger.hpp"

#include "backend.hpp"
#include "backend/backends.hpp"

#include "handlers.hpp"

typedef void (*CComsHandler_t)(const RemoteRequest &);

static constexpr std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST>
build_server_request_handlers_table() {
    std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> _server_request_handlers{0};

    _server_request_handlers[CAPIO_SERVER_REQUEST_READ]       = remote_read_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_READ_REPLY] = remote_read_reply_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_READ_BATCH] = remote_read_batch_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_READ_BATCH_REPLY] =
        remote_read_batch_reply_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_STAT]       = remote_stat_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_STAT_REPLY] = remote_stat_reply_handler;

    return _server_request_handlers;
}

[[noreturn]] void capio_remote_listener() {
    static const std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> server_request_handlers =
        build_server_request_handlers_table();

    START_LOG(gettid(), "call(rank=%d)");

    sem_wait(&internal_server_sem);
    while (true) {
        auto request = backend->read_next_request();

        server_request_handlers[request.get_code()](request);
    }
}

#endif // CAPIO_SERVER_REMOTE_LISTENER_HPP

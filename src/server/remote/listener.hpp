#ifndef CAPIO_SERVER_REMOTE_LISTENER_HPP
#define CAPIO_SERVER_REMOTE_LISTENER_HPP

#include "capio/logger.hpp"

#include "backend.hpp"
#include "backend/include.hpp"

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

inline Backend *select_backend(const std::string &backend_name, int argc, char *argv[]) {
    START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());

    if (backend_name.empty()) {
        LOG("backend selected: none");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER
                  << "Starting CAPIO with default backend (MPI) as no preferred backend was chosen"
                  << std::endl;
        return new MPIBackend(argc, argv);
    }

    if (backend_name == "mpi") {
        LOG("backend selected: mpi");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Starting CAPIO with MPI backend" << std::endl;
        return new MPIBackend(argc, argv);
    }

    if (backend_name == "fs") {
        LOG("backend selected: file system");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Starting CAPIO with file system backend"
                  << std::endl;
        return new FSBackend(argc, argv);
    }

    if (backend_name == "mpisync") {
        LOG("backend selected: mpisync");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "Starting CAPIO with MPI (SYNC) backend"
                  << std::endl;
        return new MPISYNCBackend(argc, argv);
    }
    LOG("Backend %s does not exist in CAPIO. Reverting back to the default MPI backend",
        backend_name.c_str());
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " Backend " << backend_name
              << " does not exist. Reverting to the default MPI backend" << std::endl;
    return new MPIBackend(argc, argv);
}

[[noreturn]] void capio_remote_listener(Semaphore &internal_server_sem) {
    static const std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> server_request_handlers =
        build_server_request_handlers_table();

    START_LOG(gettid(), "call()");

    internal_server_sem.lock();

    while (true) {
        auto request = backend->read_next_request();

        server_request_handlers[request.get_code()](request);
    }
}

#endif // CAPIO_SERVER_REMOTE_LISTENER_HPP

#ifndef CAPIO_SERVER_REMOTE_LISTENER_HPP
#define CAPIO_SERVER_REMOTE_LISTENER_HPP

#include "common/logger.hpp"

#include "remote/backend.hpp"
#include "remote/backend/include.hpp"
#include "remote/handlers/read.hpp"
#include "remote/handlers/stat.hpp"

typedef void (*CComsHandler_t)(const RemoteRequest &);

static constexpr std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST>
build_server_request_handlers_table() {
    std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> _server_request_handlers{0};

    _server_request_handlers[CAPIO_SERVER_REQUEST_READ]       = remote_read_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_READ_REPLY] = remote_read_reply_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_STAT]       = remote_stat_handler;
    _server_request_handlers[CAPIO_SERVER_REQUEST_STAT_REPLY] = remote_stat_reply_handler;

    return _server_request_handlers;
}

inline Backend *select_backend(const std::string &backend_name, int argc, char *argv[]) {
    START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());

    if (backend_name.empty() || backend_name == "none") {
        LOG("backend selected: none");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                  << "Starting CAPIO with default backend (none) as no preferred backend was chosen"
                  << std::endl;
        return new NoBackend(argc, argv);
    }

    if (backend_name == "mpi") {
        LOG("backend selected: mpi");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Starting CAPIO with MPI backend"
                  << std::endl;
        return new MPIBackend(argc, argv);
    }

    if (backend_name == "mpisync") {
        LOG("backend selected: mpisync");
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "Starting CAPIO with MPI (SYNC) backend"
                  << std::endl;
        return new MPISYNCBackend(argc, argv);
    }

    LOG("Backend %s does not exist in CAPIO. Reverting back to the default backend (none)",
        backend_name.c_str());
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " Backend " << backend_name
              << " does not exist. Reverting to the default backend (MPI)" << std::endl;
    return new NoBackend(argc, argv);
}

[[noreturn]] void capio_remote_listener(Semaphore &internal_server_sem) {
    static const std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> server_request_handlers =
        build_server_request_handlers_table();

    internal_server_sem.lock();

    if (typeid(*backend) == typeid(NoBackend)) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO
                  << " Backend is of type NoBackend. Stopping capio_remote_listener() execution."
                  << std::endl;
        return;
    }

    START_LOG(gettid(), "call()");

    while (true) {
        auto request = backend->read_next_request();

        server_request_handlers[request.get_code()](request);
    }
}

#endif // CAPIO_SERVER_REMOTE_LISTENER_HPP

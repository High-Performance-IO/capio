#ifndef CAPIO_SERVER_REMOTE_LISTENER_HPP
#define CAPIO_SERVER_REMOTE_LISTENER_HPP

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "common/constants.hpp"
#include "remote/backend.hpp"
#include "remote/backend/include.hpp"
#include "remote/handlers/read.hpp"
#include "remote/handlers/stat.hpp"
#include "utils/runtime_configuration.hpp"

#include <cstdlib>
#include <stdexcept>

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

inline Backend *select_backend(const CapioParsedConfig &configuration, int argc, char *argv[]) {
    const auto &backend_name = configuration.backend_name;
    START_LOG(gettid(), "call(backend_name=%s)", backend_name.c_str());

    if (backend_name.empty() || backend_name == "none") {
        LOG("backend selected: none");
        CALF_PRINT_COLOR(
            CALF_CLI_LEVEL_INFO,
            "Starting CAPIO with default backend (none) as no preferred backend was chosen");
        return new NoneBackend(argc, argv);
    }

    if (backend_name == "mpi") {
#ifdef CAPIO_HAS_MPI
        LOG("backend selected: mpi");
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Starting CAPIO with MPI backend");
        return new MPIBackend(argc, argv);
#else
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "MPI backend requested, but MPI support is not compiled in");
        exit(EXIT_FAILURE);
#endif
    }

    if (backend_name == "mtcl") {
        LOG("backend selected: MTCL");
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Starting CAPIO with MTCL backend");
        std::string protocol = CAPIO_MTCL_DEFAULT_PROTOCOL;
        std::string port     = CAPIO_MTCL_DEFAULT_PORT;
        int poll_interval    = CAPIO_MTCL_DEFAULT_POLL_INTERVAL;
        if (!configuration.backend_options.empty()) {
            const auto colon = configuration.backend_options.find(':');
            const auto at    = configuration.backend_options.find('@', colon + 1);
            if (colon == std::string::npos || at == std::string::npos || colon == 0 ||
                at == colon + 1 || at + 1 == configuration.backend_options.size() ||
                configuration.backend_options.find(':', colon + 1) != std::string::npos ||
                configuration.backend_options.find('@', at + 1) != std::string::npos) {
                CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                                 "Invalid MTCL backend options '%s'. Expected "
                                 "PROTO:PORT@POLL_INTERVAL_US.",
                                 configuration.backend_options.c_str());
                exit(EXIT_FAILURE);
            }

            protocol = configuration.backend_options.substr(0, colon);
            port     = configuration.backend_options.substr(colon + 1, at - colon - 1);
            try {
                size_t parsed = 0;
                poll_interval = std::stoi(configuration.backend_options.substr(at + 1), &parsed);
                if (parsed != configuration.backend_options.size() - at - 1 || poll_interval <= 0) {
                    throw std::invalid_argument("invalid poll interval");
                }
            } catch (const std::exception &) {
                CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                                 "Invalid MTCL poll interval in backend options '%s'. Expected a "
                                 "positive integer.",
                                 configuration.backend_options.c_str());
                exit(EXIT_FAILURE);
            }
        }
        return new MTCLBackend(protocol, port, poll_interval);
    }

    if (backend_name == "mpisync") {
#ifdef CAPIO_HAS_MPI
        LOG("backend selected: mpisync");
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Starting CAPIO with MPI (SYNC) backend");
        return new MPISYNCBackend(argc, argv);
#else
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                         "MPISYNC backend requested, but MPI support is not compiled in");
        exit(EXIT_FAILURE);
#endif
    }

    LOG("Backend %s does not exist in CAPIO. Reverting back to the default backend (none)",
        backend_name.c_str());
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                     "Backend %s does not exist. Reverting to the default backend (none)",
                     backend_name.c_str());
    return new NoneBackend(argc, argv);
}

inline void capio_remote_listener(Semaphore &internal_server_sem) {
    static const std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> server_request_handlers =
        build_server_request_handlers_table();

    internal_server_sem.lock();

    if (typeid(*backend) == typeid(NoneBackend)) {

        CALF_PRINT_COLOR(
            CALF_CLI_LEVEL_INFO,
            "backend is of type NoneBackend. Stopping capio_remote_listener() execution.");
        return;
    }

    START_LOG(gettid(), "call()");

    while (true) {
        auto request   = backend->read_next_request();
        const int code = request.get_code();
        if (code < 0 || code >= CAPIO_SERVER_NR_REQUEST ||
            server_request_handlers[code] == nullptr) {
            std::string err_msg = "Ignoring invalid remote request code: " + std::to_string(code);
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "%s", err_msg.c_str());
            continue;
        }
        server_request_handlers[code](request);
    }
}

#endif // CAPIO_SERVER_REMOTE_LISTENER_HPP

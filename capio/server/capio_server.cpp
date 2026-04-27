#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <semaphore.h>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "capiocl/parser.h"
#include "utils/capiocl_adapter.hpp"

#include "client-manager/client_manager.hpp"
#include "common/env.hpp"
#include "common/logger.hpp"
#include "common/requests.hpp"
#include "common/semaphore.hpp"
#include "remote/backend.hpp"
#include "storage/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/env.hpp"
#include "utils/types.hpp"

ClientManager *client_manager;
StorageManager *storage_manager;
Backend *backend;

#include "handlers.hpp"
#include "utils/cli_parser.hpp"
#include "utils/location.hpp"
#include "utils/signals.hpp"

#include "remote/listener.hpp"

/**
 * The capio_cl_engine is declared here to ensure that other components of the CAPIO server
 * can only access it through a const reference. This prevents any modifications to the engine
 * outside of those permitted by the capiocl::Engine class itself.
 */
capiocl::engine::Engine *capio_cl_engine;
const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
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

[[noreturn]] void capio_server(Semaphore &internal_server_sem) {
    static const std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers =
        build_request_handlers_table();

    START_LOG(gettid(), "call()");

    setup_signal_handlers();
    backend->handshake_servers();

    storage_manager->addDirectory(getpid(), get_capio_dir());

    internal_server_sem.unlock();

    auto str = std::unique_ptr<char[]>(new char[CAPIO_REQ_MAX_SIZE]);
    while (true) {
        LOG(CAPIO_LOG_SERVER_REQUEST_START);
        int code = client_manager->readNextRequest(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                           "capio_server", "Received invalid code: " + std::to_string(code));

            ERR_EXIT("Error: received invalid request code");
        }
        request_handlers[code](str.get());
        LOG(CAPIO_LOG_SERVER_REQUEST_END);
    }
}

int main(int argc, char **argv) {

    Semaphore internal_server_sem(0);

    for (const auto line : CAPIO_LOG_SERVER_BANNER) {
        server_println("", "", "", line);
    }

    const auto configuration = parseCLI(argc, argv);

    if (configuration.capio_cl_dynamic_config) {
        capio_cl_engine = new capiocl::engine::Engine();
        capio_cl_engine->startApiServer();
    } else if (!configuration.capio_cl_config_path.empty()) {
        capio_cl_engine = capiocl::parser::Parser::parse(configuration.capio_cl_config_path,
                                                         configuration.capio_cl_resolve_path,
                                                         configuration.store_all_in_memory);
    } else {
        capio_cl_engine = new capiocl::engine::Engine();
        capio_cl_engine->setWorkflowName(get_capio_workflow_name());
    }

    if (configuration.store_all_in_memory) {
        capio_cl_engine->setAllStoreInMemory();
    }

    capio_cl_engine->print();

    backend = select_backend(configuration.backend_name, argc, argv);

    START_LOG(gettid(), "call()");

    open_files_location();

    shm_canary      = new CapioShmCanary(capio_cl_engine->getWorkflowName());
    storage_manager = new StorageManager();
    client_manager  = new ClientManager();

    std::thread server_thread(capio_server, std::ref(internal_server_sem));
    LOG("capio_server thread started");
    std::thread remote_listener_thread(capio_remote_listener, std::ref(internal_server_sem));
    LOG("capio_remote_listener thread started.");
    server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_STATUS,
                   "main", "Server instance successfully started!");
    server_thread.join();
    remote_listener_thread.join();

    delete backend;
    return 0;
}
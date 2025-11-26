#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <semaphore.h>
#include <singleheader/simdjson.h>
#include <string>
#include <unistd.h>
#include <unordered_set>

#include <capiocl.hpp>
#include <engine.h>
#include <parser.h>
capiocl::engine::Engine *capio_cl_engine;

#include <include/utils/configuration.hpp>
CapioGlobalConfiguration *capio_global_configuration;

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"
#include "include/api-server/api-server.hpp"

#include <include/client-manager/client_manager.hpp>
#include <include/client-manager/request_handler_engine.hpp>
#include <include/file-manager/file_manager.hpp>
#include <include/file-manager/fs_monitor.hpp>
#include <include/storage-service/capio_storage_service.hpp>
#include <include/utils/command_line_parser.hpp>
#include <include/utils/signals.hpp>

int main(int argc, char **argv) {
    std::cout << CAPIO_LOG_SERVER_BANNER;
    capio_global_configuration                        = new CapioGlobalConfiguration();
    capio_global_configuration->CAPIO_SERVER_MAIN_PID = gettid();

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "Started server with PID: " +
                       std::to_string(capio_global_configuration->CAPIO_SERVER_MAIN_PID));

    char resolve_prefix[PATH_MAX]{0};
    const std::string config_path = parseCLI(argc, argv, resolve_prefix);

    START_LOG(gettid(), "call()");
    setup_signal_handlers();

    capio_cl_engine =
        capiocl::parser::Parser::parse(config_path, std::filesystem::path(resolve_prefix));

    capio_global_configuration->workflow_name = capio_cl_engine->getWorkflowName();

    shm_canary              = new CapioShmCanary(capio_global_configuration->workflow_name);
    api_server              = new CapioAPIServer(6666);
    file_manager            = new CapioFileManager();
    fs_monitor              = new FileSystemMonitor();
    client_manager          = new ClientManager();
    request_handlers_engine = new RequestHandlerEngine();
    storage_service         = new CapioStorageService();

    capio_cl_engine->print();

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "server initialization completed!");

    request_handlers_engine->start();

    sig_term_handler(SIGTERM, nullptr, nullptr);

    exit(EXIT_SUCCESS);
}
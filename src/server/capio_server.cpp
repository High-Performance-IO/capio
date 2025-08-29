#include <dirent.h>
#include <fcntl.h>
#include <semaphore.h>
#include <singleheader/simdjson.h>
#include <unistd.h>

#include <algorithm>
#include <args.hxx>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "utils/configuration.hpp"

#include "utils/types.hpp"

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "capio/semaphore.hpp"

#include "client-manager/request_handler_engine.hpp"
#include "utils/signals.hpp"

#include "storage-service/capio_storage_service.hpp"

#include "file-manager/file_manager.hpp"

#include "communication-service/CapioCommunicationService.hpp"

#include <utils/parser.hpp>

int main(int argc, char **argv) {
    std::cout << CAPIO_LOG_SERVER_BANNER;

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "Started server with PID: " +
                       std::to_string(capio_global_configuration->CAPIO_SERVER_MAIN_PID));

    char resolve_prefix[PATH_MAX]{0};
    const std::string config_path = parseCLI(argc, argv, resolve_prefix);

    START_LOG(gettid(), "call()");
    setup_signal_handlers();

    capio_cl_engine         = JsonParser::parse(config_path, std::filesystem::path(resolve_prefix));
    shm_canary              = new CapioShmCanary(capio_global_configuration->workflow_name);
    file_manager            = new CapioFileManager();
    fs_monitor              = new FileSystemMonitor();
    request_handlers_engine = new RequestHandlerEngine();
    storage_service         = new CapioStorageService();

    capio_cl_engine->print();

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "server initialization completed!");

    request_handlers_engine->start();

    sig_term_handler(SIGTERM, nullptr, nullptr);

    return 0;
}
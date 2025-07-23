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

/*
 * Variables required to be globally available
 * to all classes and subclasses.
 */
std::string workflow_name;
pid_t CAPIO_SERVER_MAIN_PID;
inline bool StoreOnlyInMemory = false;
char node_name[HOST_NAME_MAX];

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
    gethostname(node_name, HOST_NAME_MAX);
    CAPIO_SERVER_MAIN_PID = gettid();
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "Started server with PID: " << CAPIO_SERVER_MAIN_PID << std::endl;
    const std::string config_path = parseCLI(argc, argv);

    START_LOG(gettid(), "call()");
    setup_signal_handlers();

    capio_cl_engine         = JsonParser::parse(config_path);
    shm_canary              = new CapioShmCanary(workflow_name);
    file_manager            = new CapioFileManager();
    fs_monitor              = new FileSystemMonitor();
    request_handlers_engine = new RequestHandlerEngine();
    storage_service         = new CapioStorageService();

    capio_cl_engine->print();

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "server initialization completed!" << std::endl
              << std::flush;

    request_handlers_engine->start();

    return 0;
}
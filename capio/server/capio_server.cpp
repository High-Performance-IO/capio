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
#include "client-manager/request_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "storage/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/thread_manager.hpp"
#include "utils/types.hpp"

ClientManager *client_manager;
StorageManager *storage_manager;
FsStorageManager *fs_storage_manager;
Backend *backend;
DiscoveryService *discovery_service;
HomeNodeService *home_node_service;

#include "remote/home_node_service.hpp"
#include "utils/runtime_configuration.hpp"
#include "utils/signals.hpp"

#include "remote/listener.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

/**
 * The capio_cl_engine is declared here to ensure that other components of the CAPIO server
 * can only access it through a const reference. This prevents any modifications to the engine
 * outside of those permitted by the capiocl::Engine class itself.
 */
capiocl::engine::Engine *capio_cl_engine;
const capiocl::engine::Engine &CapioCLEngine::get() { return *capio_cl_engine; }

int main(int argc, char **argv) {

    for (const auto line : CAPIO_LOG_SERVER_BANNER) {
        CALF_PRINT("%s", line);
    }

    RuntimeConfiguration::initialize(argc, argv);

    START_LOG(gettid(), "call()");
    capio_cl_engine = RuntimeConfiguration::get().createCapioClEngine();

    UPDATE_CALF_WORKFLOW_NAME(capio_cl_engine->getWorkflowName());

    capio_cl_engine->print();

    discovery_service = select_discovery_service();
    backend           = select_backend(argc, argv);

    home_node_service =
        new HomeNodeService(RuntimeConfiguration::get().getLocationQueryTimeout(),
                            RuntimeConfiguration::get().getLocationRequeryInterval());

    storage_manager    = new StorageManager();
    fs_storage_manager = new FsStorageManager();
    client_manager     = new ClientManager();

    setup_signal_handlers();

    // The termination signals must reach only this thread, which only waits, and never one in the
    // middle of a request (see sig_term_handler). Every thread of the server starts in this scope,
    // also the ones the handshake starts (the discovery of MTCL), so each inherits a mask with
    // those signals masked. A signal that arrives meanwhile stays pending, and this thread
    // receives it when the scope ends.
    std::thread requests_thread, remote_listener_thread;
    {
        const TerminationSignalMasker masker;
        backend->handshake_servers();

        // "/" is the CAPIO_DIR of the clients: an empty directory, local to every server. It
        // exists before the first request, of a client or of another server.
        storage_manager->createDirectory(getpid(), "/");

        // The handshake is over: the other servers can be listened to
        requests_thread = std::thread(ClientRequestManager::start);
        LOG("client requests thread started");
        remote_listener_thread = std::thread(capio_remote_listener);
        LOG("capio_remote_listener thread started.");
    }
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_STATUS, "Server instance successfully started!");
    requests_thread.join();
    remote_listener_thread.join();

    return 0;
}

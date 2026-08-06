#include "common/logger.hpp"
#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

extern Backend *backend;

void DiscoveryService::start(const std::string &token, unsigned int adv_delay) const {

    if (token.empty()) {
        throw std::runtime_error("Advertisement token is empty");
    }

    discovery_backend->start(token, adv_delay);

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "DiscoveryService will advertise " + token +
                                                        " every " + std::to_string(adv_delay) +
                                                        "ms.");
}
void DiscoveryService::stop() const { discovery_backend->stop(); }

DiscoveryService::DiscoveryService(const std::string protocol, const std::string &mcast_addr,
                                   const unsigned int mcast_port,
                                   const std::string token_directory) {
    if (protocol != CAPIO_MCAST_PROTO_FLAG && protocol != CAPIO_FS_PROTO_FLAG) {
        throw std::runtime_error("Unknown discovery protocol: " + protocol);
    }

    shm_canary = new CapioShmCanary(CapioCLEngine::get().getWorkflowName());

    if (protocol == CAPIO_MCAST_PROTO_FLAG) {
        discovery_backend = new MulticastDiscoveryService(mcast_addr, mcast_port);
    } else if (protocol == CAPIO_FS_PROTO_FLAG) {
        discovery_backend = new FSDiscoveryService(token_directory);
    }

    server_println("initialization completed with " + protocol + " discovery.",
                   CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_STATUS,
                   "DiscoveryService");
}

DiscoveryService::~DiscoveryService() {
    // if destructor is called before stop(), then stop the the service first.
    discovery_backend->stop();
    // delete backend
    delete discovery_backend;
    // delete shm canary
    delete shm_canary;
    server_println("teardown completed.", CapioCLEngine::get().getWorkflowName(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "DiscoveryService");
}

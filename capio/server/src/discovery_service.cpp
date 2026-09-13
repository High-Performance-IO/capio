#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"

extern Backend *backend;

DiscoveryService::DiscoveryService(std::unique_ptr<DiscoveryInterface> discovery_backend)
    : shm_canary(std::make_unique<CapioShmCanary>(CapioCLEngine::get().getWorkflowName())),
      discovery_interface(std::move(discovery_backend)) {}

DiscoveryService::~DiscoveryService() {
    // if destructor is called before stop(), then stop the the service first.
    discovery_interface->stop();
}

void DiscoveryService::start(const std::string &token, unsigned int adv_delay) const {

    if (token.empty()) {
        throw std::runtime_error("Advertisement token is empty");
    }

    discovery_interface->start(token, adv_delay);

    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "DiscoveryService will advertise %s every %u ms.",
                     token.c_str(), adv_delay);
}

void DiscoveryService::stop() const { discovery_interface->stop(); }

#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <filesystem>
#include <string>
#include <thread>

#include "common/constants.hpp"
#include "utils/cli_parser.hpp"
#include "utils/shm_canary.hpp"

/**
 * @brief Interface implemented by CAPIO discovery backends.
 *
 * A discovery backend advertises the local server connection token and discovers tokens published
 * by other server instances.
 */
class DiscoveryServiceInterface {
  protected:
    /// @brief Variable used to signal termination to child threads
    bool terminate = false;
    /// @brief Token to be advertised by this server
    std::string advertisement_token;

  public:
    /// @brief Destroy a discovery backend.
    virtual ~DiscoveryServiceInterface() = default;

    /**
     * @brief Start advertising and discovering server tokens.
     * @param token Connection token advertised by this server.
     * @param delay Delay in milliseconds between advertisements or filesystem scans.
     */
    virtual void start(const std::string &token, unsigned int delay) = 0;

    /// @brief Stop all work performed by the discovery backend.
    virtual void stop() = 0;
};

/**
 * @brief Coordinates local instance protection and remote server discovery.
 *
 * The shared-memory canary prevents two CAPIO servers with the same workflow name from running on
 * one node. The selected discovery backend finds other server instances and passes their connection
 * tokens to the active communication backend.
 */
class DiscoveryService {

    /// @brief Canary variable to detect other server instances running locally that are logically
    /// equivalent to the one starting up
    std::unique_ptr<CapioShmCanary> shm_canary;

    /// @brief Selected multicast or filesystem discovery backend.
    std::unique_ptr<DiscoveryServiceInterface> discovery_backend;

  public:
    /**
     * @param discovery_backend Backend that will execute the actual discovery of other running
     * instances
     * @throws std::runtime_error If @p protocol is unsupported or the selected backend cannot be
     * initialized.
     */
    explicit DiscoveryService(std::unique_ptr<DiscoveryServiceInterface> discovery_backend);

    /// @brief Stop discovery and destroy the selected backend and shared-memory canary.
    ~DiscoveryService();

    /**
     * @brief Start the selected discovery backend.
     *
     * Multicast discovery broadcasts and listens for tokens. Filesystem discovery writes the local
     * token file and scans the configured directory for tokens from other servers.
     * @param token Connection token provided by the communication backend.
     * @param adv_delay Delay in milliseconds between advertisements or filesystem scans.
     * @throws std::runtime_error If @p token is empty.
     */
    void start(const std::string &token, unsigned int adv_delay) const;

    /**
     * @brief Stop advertising and discovering server tokens.
     * @note The shared-memory canary remains active until this object is destroyed.
     */
    void stop() const;
};

#include "discovery/fs.h"
#include "discovery/multicast.h"

inline DiscoveryService *selct_discovery_backend(const CapioParsedConfig &config) {
    if (config.discovery_protocol == CAPIO_MCAST_PROTO_FLAG) {
        return new DiscoveryService(
            std::make_unique<MulticastDiscoveryService>(config.mcast_addr, config.mcast_port));
    } else {
        return new DiscoveryService(std::make_unique<FSDiscoveryService>(config.token_directory));
    }
}

#endif // CAPIO_DISCOVERY_HPP

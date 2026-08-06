#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <filesystem>
#include <string>
#include <thread>

#include "common/constants.hpp"
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
    virtual void start(std::string token, unsigned int delay) = 0;

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
    CapioShmCanary *shm_canary;

    /// @brief Selected multicast or filesystem discovery backend.
    DiscoveryServiceInterface *discovery_backend;

  public:
    /**
     * @brief Construct a discovery service and its selected backend.
     * @param protocol Discovery protocol: `mcast` or `fs`. Defaults to `mcast`.
     * @param mcast_addr Multicast address used when @p protocol is `mcast`.
     * @param mcast_port Multicast port used when @p protocol is `mcast`.
     * @param token_directory Token directory used when @p protocol is `fs`.
     * @throws std::runtime_error If @p protocol is unsupported or the selected backend cannot be
     * initialized.
     */
    explicit DiscoveryService(std::string protocol          = CAPIO_MCAST_PROTO_FLAG,
                              const std::string &mcast_addr = CAPIO_MCAST_ADV_DEFAULT_ADDR,
                              unsigned int mcast_port       = CAPIO_MCAST_ADV_DEFAULT_PORT,
                              std::string token_directory   = ".capio_tokens/");

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

/**
 * @brief Discovers CAPIO servers by exchanging connection tokens over UDP multicast.
 */
class MulticastDiscoveryService : public DiscoveryServiceInterface {

    /// @brief Variable used to signal termination to child threads
    bool terminate = false;

    /// @brief Handle for thread advertising this server instance
    std::thread *advertisement_thread = nullptr;

    /// @brief Handle for multicast based discovery thread
    std::thread *mcast_listener_thread = nullptr;

    /// @brief Multicast address
    const std::string capio_multicast_adv_address;

    /// @brief multicast port
    const unsigned int capio_multicast_adv_port;

  public:
    /**
     * @brief Construct a multicast discovery backend.
     * @param mcast_addr Multicast group address used to exchange tokens.
     * @param mcast_port UDP port used to exchange tokens.
     */
    MulticastDiscoveryService(const std::string &mcast_addr, unsigned int mcast_port);

    /// @brief Destroy the multicast discovery backend.
    ~MulticastDiscoveryService();

    /**
     * @brief Start the multicast listener and advertisement threads.
     * @param token Connection token advertised by this server.
     * @param adv_delay Delay in milliseconds between advertisements.
     */
    void start(std::string token, unsigned int adv_delay);

    /// @brief Stop and join the multicast listener and advertisement threads.
    void stop();
};

/**
 * @brief Discovers CAPIO servers through token files in a shared directory.
 */
class FSDiscoveryService : public DiscoveryServiceInterface {

    /// @brief Directory to look into for CAPIO tokens
    std::filesystem::path token_directory_path;
    /// @brief This server instance token filename
    std::filesystem::path token_filename;

    /// @brief Handle for file system based discovery thread
    std::thread *fs_listener_thread = nullptr;

  public:
    /**
     * @brief Construct a filesystem discovery backend.
     * @param token_directory Directory used to publish and discover token files.
     * @throws std::runtime_error If @p token_directory is empty.
     */
    FSDiscoveryService(const std::string &token_directory);

    /// @brief Remove this server's token file and destroy the filesystem discovery backend.
    ~FSDiscoveryService();

    /**
     * @brief Publish this server's token and start scanning for other token files.
     * @param token Connection token published by this server.
     * @param adv_delay Delay in milliseconds between directory scans.
     */
    void start(std::string token, unsigned int adv_delay);

    /// @brief Stop and join the filesystem scanning thread.
    void stop();
};

#endif // CAPIO_DISCOVERY_HPP

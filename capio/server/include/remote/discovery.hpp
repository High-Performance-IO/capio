#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <string>
#include <thread>

#include "utils/shm_canary.hpp"

/**
 * Discovery service. Responsible for:
 * - Detect other server instances running in the same node with the same workflow name (and halts
 * startup if it finds one)
 * - Detect other remote running server instances of capio servers and issue commands to the backend
 * to open a connection with them as soon as they are found.
 */
class DiscoveryService {

    /// @brief Variable used to signal termination to child threads
    bool terminate = false;

    /// @brief Handle for multicast based discovery thread
    std::thread *mcast_listener_thread = nullptr;
    /// @brief Handle for file system based discovery thread
    std::thread *fs_listener_thread    = nullptr;
    /// @brief Handle for thread advertising this server instance
    std::thread *advertisement_thread  = nullptr;

    /// @brief Token to be advertised by this server
    std::string advertisement_token;

    /// @brief Canary variable to detect other server instances running locally that are logically
    /// equivalent to the one starting up
    CapioShmCanary *shm_canary;

    /// @brief Directory to look into for CAPIO tokens
    std::filesystem::path token_directory_path;
    /// @brief This server instance token filename
    std::filesystem::path token_filename;

    /// @brief Multicast address
    const std::string capio_multicast_adv_address;

    /// @brief multicast port
    const unsigned int capio_multicast_adv_port;

  public:
    /**
     * Construct a new Discovery Service class
     * @param mcast_addr Address to send and receive aliveness token from other servers
     * @param mcast_port Port to send and receive aliveness token from other servers
     */
    explicit DiscoveryService(const std::string &mcast_addr = CAPIO_MCAST_ADV_DEFAULT_ADDR,
                              unsigned int mcast_port       = CAPIO_MCAST_ADV_DEFAULT_PORT);

    /// @brief Default destructor
    ~DiscoveryService();

    /**
     * @brief Configures and starts the discovery service to advertise and scan for tokens.
     *
     * Sets the advertisement token used by other server instances to establish a connection.
     * The token must conform to the specific backend requirements for incoming connections.
     * * @note The token is not passed via the constructor because the Discovery Service
     * must be instantiated before the Backend provides the token.
     *
     * Once called, this method:
     * 1. Stores the current token in a hidden file within a designated directory.
     * 2. Initiates multicast traffic to advertise the local token.
     * 3. Scans the hidden directory for aliveness tokens from other servers.
     *
     * @param adv_delay The interval (in milliseconds/seconds) between advertisement broadcasts.
     * @param token The authentication or identification string provided by the backend.
     * @param token_directory directory to store capio aliveness tokens
     */
    void start(unsigned int adv_delay, const std::string &token,
               const std::string &token_directory = ".capio_tokens/");

    /**
     * Stop current server instance from advertising itself and from receiving advertisements from
     * other server instances.
     *
     * NOTE: this method does not destroy the CAPIO canary variable. for that the destruction of the
     * class instance is required.
     */
    void stop();
};

#endif // CAPIO_DISCOVERY_HPP
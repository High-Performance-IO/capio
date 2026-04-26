#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <string>
#include <thread>

#include "utils/shm_canary.hpp"

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

    std::filesystem::path token_directory_path = ".capio_tokens/";
    std::filesystem::path token_filename;

  public:
    /// @brief Default constructor
    DiscoveryService();

    /// @brief Default destructor
    ~DiscoveryService();

    /**
     * Set the token to be advertised so that other server instance may connect to this instance.
     * Token needs to be provided by an instance of a backend, according to backend specification
     * for incoming connection.
     *
     * Once the token is set, a new hidden file with the current token is stored within a hidden
     * directory.
     * @param token
     */
    void setAdvertisementToken(const std::string &token);

    /**
     * Start to advertise the token, and to scan for tokens from other servers. Advertisement works
     * by sending multicast traffic, and by scanning files contained within the hidden directory
     * with aliveness tokens.
     * @param adv_delay Delay between each advertisement.
     */
    void start(unsigned int adv_delay);

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
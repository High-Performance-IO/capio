#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include "common/shm.hpp"
#include <string>
#include <thread>

class CapioShmCanary {
    int _shm_id;
    std::string _canary_name;

  public:
    explicit CapioShmCanary(std::string capio_workflow_name);
    ~CapioShmCanary();
};

class DiscoveryService {
    bool terminate = false;

    /// @brief Handle for thread listening for other server instances
    std::thread *listener_thread      = nullptr;
    /// @brief Handle for thread advertising this server instance
    std::thread *advertisement_thread = nullptr;

    /// @brief Token to be advertised by this server
    std::string advertisement_token;

    /// @brief Canary variable to detect other server instances running locally that are logically
    /// equivalent to the one starting up
    CapioShmCanary *shm_canary;

  public:
    DiscoveryService();
    ~DiscoveryService();

    /**
     * Set the token to be advertised so that other server instance may connect to this instance.
     * Token needs to be provided by an instance of a backend, according to backend specification
     * for incoming connection
     * @param token
     */
    void setAdvertisementToken(const std::string &token);

    /**
     * Start to advertise the token, and to scan for tokens from other servers
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
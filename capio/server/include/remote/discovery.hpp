#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <string>
#include <thread>

class DiscoveryService {
    bool terminate = false;

    /// @brief Handle for thread listening for other server instances
    std::thread *listener_thread      = nullptr;
    /// @brief Handle for thread advertising this server instance
    std::thread *advertisement_thread = nullptr;

    /// @brief Token to be advertised by this server
    std::string advertisement_token;

  public:
    DiscoveryService() = default;
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
};

#endif // CAPIO_DISCOVERY_HPP

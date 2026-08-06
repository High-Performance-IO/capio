#ifndef CAPIO_DISCOVERY_MULTICAST_HPP
#define CAPIO_DISCOVERY_MULTICAST_HPP
#include <string>

/**
 * @brief Discovers CAPIO servers by exchanging connection tokens over UDP multicast.
 */
class MulticastDiscoveryInterface : public DiscoveryInterface {

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
     * @brief Construct a multicast discovery interface.
     * @param mcast_addr Multicast group address used to exchange tokens.
     * @param mcast_port UDP port used to exchange tokens.
     */
    MulticastDiscoveryInterface(const std::string &mcast_addr, unsigned int mcast_port);

    /// @brief Destroy the multicast discovery interface.
    ~MulticastDiscoveryInterface() override;

    /**
     * @brief Start the multicast listener and advertisement threads.
     * @param token Connection token advertised by this server.
     * @param adv_delay Delay in milliseconds between advertisements.
     */
    void start(const std::string &token, unsigned int adv_delay) override;

    /// @brief Stop and join the multicast listener and advertisement threads.
    void stop() override;
};

#endif // CAPIO_DISCOVERY_MULTICAST_HPP

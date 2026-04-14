#ifndef CAPIO_DISCOVERY_HPP
#define CAPIO_DISCOVERY_HPP

#include <netinet/in.h>
#include <string>
#include <thread>

class DiscoveryService {
    bool terminate = false;

    std::thread *listener_thread;

  public:
    DiscoveryService();
    ~DiscoveryService();

    static void advertise(const std::string &token);
};

#endif // CAPIO_DISCOVERY_HPP

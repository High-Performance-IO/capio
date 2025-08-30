#ifndef MULTICAST_CONTROL_PLANE_HPP
#define MULTICAST_CONTROL_PLANE_HPP
#include <capio/logger.hpp>
#include <include/communication-service/control-plane/capio_control_plane.hpp>
#include <mutex>
#include <thread>
#include <vector>

class MulticastControlPlane : public CapioControlPlane {
    bool *continue_execution;
    std::thread *discovery_thread, *controlpl_incoming;
    std::vector<std::string> token_used_to_connect;
    std::mutex *token_used_to_connect_mutex;
    char ownHostname[HOST_NAME_MAX] = {0};

    static void multicast_server_aliveness_thread(const bool *continue_execution,
                                                  std::vector<std::string> *token_used_to_connect,
                                                  std::mutex *token_used_to_connect_mutex,
                                                  int dataplane_backend_port);

    static void multicast_control_plane_incoming_thread(const bool *continue_execution);

  public:
    explicit MulticastControlPlane(int dataplane_backend_port);

    ~MulticastControlPlane() override;

    void notify_all(event_type event, const std::filesystem::path &path) override;
};

#endif // MULTICAST_CONTROL_PLANE_HPP

#ifndef FS_CONTROL_PLANE_HPP
#define FS_CONTROL_PLANE_HPP

#include <include/communication-service/control-plane/capio_control_plane.hpp>
#include <mutex>
#include <thread>
#include <vector>

class FSControlPlane : public CapioControlPlane {
    char ownHostname[HOST_NAME_MAX] = {0};
    int _backend_port;
    bool *continue_execution;
    std::thread *thread;
    std::vector<std::string> token_used_to_connect;
    std::mutex *token_used_to_connect_mutex;

    void generate_aliveness_token(int port) const;

    void delete_aliveness_token();

    /*
     * Monitor the file system for the presence of tokens.
     */
    static void fs_server_aliveness_detector_thread(const bool *continue_execution,
                                                    std::vector<std::string> *token_used_to_connect,
                                                    std::mutex *token_used_to_connect_mutex);

  public:
    explicit FSControlPlane(int backend_port);

    ~FSControlPlane();

    void notify_all(event_type event, const std::filesystem::path &path);
};

#endif // FS_CONTROL_PLANE_HPP

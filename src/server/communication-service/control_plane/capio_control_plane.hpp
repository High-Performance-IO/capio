#ifndef CAPIO_CONTROL_PLANE_HPP
#define CAPIO_CONTROL_PLANE_HPP

#include <filesystem>

class CapioControlPlane {
public:
    typedef enum { CREATE, DELETE, WRITE } event_type;

    virtual ~CapioControlPlane() = default;

    /**
     * Notify a single host of the occurrence of an event
     * @param event
     * @param path
     * @param hostname_target
     */
    void notify(event_type event, const std::filesystem::path &path,
                const std::string &hostname_target) {
    }

    /**
     * Notify all nodes of the occurence of an event
     * @param event
     * @param path
     */
    virtual void notify_all(event_type event, const std::filesystem::path &path) = 0;
};

inline CapioControlPlane *capio_control_plane;

#endif // CAPIO_CONTROL_PLANE_HPP
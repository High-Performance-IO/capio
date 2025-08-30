#include <capio/logger.hpp>
#include <climits>
#include <filesystem>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/communication-service/control-plane/capio_control_plane.hpp>
#include <include/file-manager/file_manager.hpp>
#include <include/storage-service/capio_storage_service.hpp>

void create_handler(const char *const str) {
    pid_t tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    std::string path_str(path);
    std::string name(client_manager->get_app_name(tid));

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);
    /**
     * See write.hpp for a reason for which the method on file manager is not being invoked
     */
    // file_manager->unlockThreadAwaitingCreation(path);

    capio_cl_engine->addProducer(path, name);
    client_manager->register_produced_file(tid, path_str);
    storage_service->createMemoryFile(path);
    capio_control_plane->notify_all(CapioControlPlane::CREATE, path_str);
}

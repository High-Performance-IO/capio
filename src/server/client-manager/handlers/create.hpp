#ifndef CAPIO_CREATE_HPP
#define CAPIO_CREATE_HPP
#include "storage-service/capio_storage_service.hpp"

/**
 * @brief Handle the create systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void create_handler(const char *const str) {
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
}

#endif // CAPIO_CREATE_HPP

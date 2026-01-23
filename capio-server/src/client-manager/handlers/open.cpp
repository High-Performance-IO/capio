#include <capio/logger.hpp>
#include <climits>
#include <filesystem>
#include <capiocl.hpp>
#include <capiocl/engine.h>
#include <include/file-manager/file_manager.hpp>
#include <include/storage-service/capio_storage_service.hpp>
#include <include/client-manager/client_manager.hpp>
extern capiocl::engine::Engine *capio_cl_engine;
extern ClientManager *client_manager;
extern CapioFileManager *file_manager;
extern CapioStorageService *storage_service;
void open_handler(const char *const str) {
    pid_t tid;
    int fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s", tid, fd, path);

    if (capio_cl_engine->isExcluded(path)) {
        LOG("File should not be handled as it is excluded!");
        client_manager->reply_to_client(tid, 0);
        return;
    }

    const auto app_name = client_manager->get_app_name(tid);

    if (capio_cl_engine->isProducer(path, app_name)) {
        LOG("Thread is producer. allowing to continue with open");
        client_manager->reply_to_client(tid, 1);
        storage_service->createMemoryFile(path);
        return;
    }

    if (std::filesystem::exists(path)) {
        LOG("File already exists! allowing to continue with open");
        client_manager->reply_to_client(tid, 1);

        /*
         * At this point, the file that needs to be created more likely than not is not local to the
         * machine. As such, we call the creation of a new CapioRemoteFile
         */
        storage_service->createRemoteFile(path, {});
        return;
    }

    LOG("File does not yet exists. halting operation and adding it to queue");
    file_manager->addThreadAwaitingCreation(path, tid);
}
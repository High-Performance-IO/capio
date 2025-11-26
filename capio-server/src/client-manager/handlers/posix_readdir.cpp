#include <capio/logger.hpp>
#include <climits>
#include <capiocl.hpp>
#include <engine.h>
#include <include/file-manager/file_manager.hpp>
#include <include/storage-service/capio_storage_service.hpp>
#include <include/client-manager/client_manager.hpp>
extern capiocl::engine::Engine *capio_cl_engine;

void posix_readdir_handler(const char *const str) {
    pid_t pid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &pid, path);
    START_LOG(gettid(), "call(pid=%d, path=%s", pid, path);

    if (capio_cl_engine->isExcluded(path)) {
        LOG("Path is excluded. Creating commit token to avoid starvation");
        CapioFileManager::setCommitted(path);
        client_manager->reply_to_client(pid, 6);
        storage_service->reply_to_client_raw(pid, "<NONE>", 6);
    }

    const auto metadata_token = CapioFileManager::getMetadataPath(path);
    LOG("sending to pid %ld token path of %s", pid, metadata_token.c_str());

    client_manager->reply_to_client(pid, metadata_token.length());
    storage_service->reply_to_client_raw(pid, metadata_token.c_str(), metadata_token.length());
}

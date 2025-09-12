#include <capio/logger.hpp>
#include <climits>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/file-manager/file_manager.hpp>
#include <include/storage-service/capio_storage_service.hpp>

void posix_readdir_handler(const char *const str) {
    pid_t pid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &pid, path);
    START_LOG(gettid(), "call(pid=%d, path=%s", pid, path);

    if (capio_cl_engine->isExcluded(path)) {
        LOG("Path is excluded. Creating commit token to avoid starvation");
        CapioFileManager::setCommitted(path);
    }

    const auto metadata_token = CapioFileManager::getMetadataPath(path);
    LOG("sending to pid %ld token path of %s", pid, metadata_token.c_str());

    client_manager->reply_to_client(pid, metadata_token.length());
    storage_service->reply_to_client_raw(pid, metadata_token.c_str(), metadata_token.length());
}

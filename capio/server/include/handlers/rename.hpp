#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP
#include "client-manager/client_manager.hpp"
#include "storage/storage_service.hpp"
#include "utils/location.hpp"

extern StorageService *storage_service;
extern ClientManager *client_manager;

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s)", tid, oldpath.c_str(),
              newpath.c_str());

    // FIXME: this doesn't work if a node renames a file handled by another node
    if (auto c_file_opt = storage_service->getFile(oldpath)) {
        storage_service->renameFile(oldpath, newpath);
        delete_from_files_location(oldpath);
        if (!get_file_location_opt(newpath)) {
            write_file_location(newpath);
        }
        rename_file_location(oldpath, newpath);
        client_manager->replyToClient(tid, 0);
    } else {
        client_manager->replyToClient(tid, 1);
    }
}

void rename_handler(const char *const str) {
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];
    int tid;
    sscanf(str, "%s %s %d", oldpath, newpath, &tid);
    handle_rename(tid, oldpath, newpath);
}

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

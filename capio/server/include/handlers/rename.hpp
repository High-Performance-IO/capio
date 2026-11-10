#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP
#include "client-manager/client_manager.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/filesystem.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s)", tid, oldpath.c_str(),
              newpath.c_str());

    // FIXME: this doesn't work if a node renames a file handled by another node
    if (storage_manager->tryGet(oldpath)) {
        storage_manager->rename(oldpath, newpath);
        // The renaming process produced the file it renames: commit it under its new name.
        if (const auto &produced = client_manager->getProducedFiles(tid);
            produced.find(oldpath) != produced.end()) {
            client_manager->removeProducedFile(tid, oldpath);
            client_manager->registerProducedFile(tid, newpath);
        }
        // Drop the old name from its directory, and the entry of the file newpath replaces, if
        // any: updateDirectory appends newpath's entry without checking for one already there.
        storage_manager->removeFromDirectory(oldpath);
        storage_manager->removeFromDirectory(newpath);
        if (storage_manager->tryGet(get_parent_dir_path(newpath))) {
            storage_manager->updateDirectory(tid, newpath);
        }
        home_node_service->rename(oldpath, newpath);
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

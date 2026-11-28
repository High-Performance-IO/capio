#include "handlers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "remote/home_node_service.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/server_state.hpp"

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

void handle_leave(int tid, const std::filesystem::path &oldpath) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s)", tid, oldpath.c_str());
    if (!storage_manager->tryGet(oldpath)) {
        client_manager->replyToClient(tid, 1);
        return;
    }
    static unsigned long left = 0; // the main thread handles every rename
    storage_manager->rename(oldpath, "left:" + std::to_string(++left));
    client_manager->removeProducedFile(tid, oldpath);
    storage_manager->removeFromDirectory(oldpath);
    home_node_service->remove(oldpath);
    client_manager->replyToClient(tid, 0);
}

void rename_handler(const char *const str) {
    int tid, consumed = 0;
    size_t old_size;
    if (sscanf(str, "%d %zu%n", &tid, &old_size, &consumed) != 2) {
        return;
    }
    const std::string names = request_text(tid, str, consumed);
    if (names.size() < old_size) {
        return; // the request was truncated
    }
    if (names.size() == old_size) {
        handle_leave(tid, names);
        return;
    }
    handle_rename(tid, names.substr(0, old_size), names.substr(old_size));
}

void rename_fs_handler(const char *const str) {
    int tid, old_in, new_in, consumed = 0;
    size_t old_size;
    if (sscanf(str, "%d %d %d %zu%n", &tid, &old_in, &new_in, &old_size, &consumed) != 4) {
        return;
    }
    const std::string text = request_text(tid, str, consumed);
    const std::string_view names(text);
    START_LOG(gettid(), "call(tid=%d, names=%s)", tid, text.c_str());
    if (names.size() < old_size) {
        LOG("The names are cut: the request was too long");
        return;
    }
    fs_storage_manager->rename(tid, fs_normal_path(names.substr(0, old_size)), old_in != 0,
                               fs_normal_path(names.substr(old_size)), new_in != 0);
}

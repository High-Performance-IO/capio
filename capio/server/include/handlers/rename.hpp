#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP
#include "client-manager/client_manager.hpp"
#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"
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

/**
 * @brief A file renamed out of CAPIO_DIR: its name leaves the server, but the file stays, under a
 * key that no virtual path can name (they all start with "/"), for the descriptors still open on
 * it, as a file unlinked while open.
 * TODO: write the file to its new name on the file system (#343)
 */
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

/**
 * @brief Rename of the memory variant: "tid old_size old_pathnew_path".
 *
 * The two names are written one after the other with no separator, since a path can contain any
 * character: the first old_size bytes are the old name, the rest is the new one. As in
 * rename_fs_handler. No new name means the file left CAPIO_DIR.
 */
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

/**
 * @brief A rename done by an FS client, sent after the kernel has done it. Format:
 * "tid old_in new_in old_size old_pathnew_path", where old_in and new_in are 1 if that name is
 * inside CAPIO_DIR, and the two names are written back to back, the old one being old_size bytes.
 *
 * Everything the server knows about the file moves to the new name, and the new name's commit
 * rule is applied. This is what makes "write f.tmp, close it, rename it to f" commit f. No reply.
 */
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

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

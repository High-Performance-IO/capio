#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP

#include "utils/location.hpp"

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s)", tid, oldpath.c_str(),
              newpath.c_str());

    // FIXME: this doesn't work if a node renames a file handled by another node
    if (auto c_file_opt = get_capio_file_opt(oldpath)) {
        rename_capio_file(oldpath, newpath);
        for (auto &pair : writers) {
            auto node = pair.second.extract(oldpath);
            if (!node.empty()) {
                node.key() = newpath;
                pair.second.insert(std::move(node));
            }
        }
        delete_from_files_location(oldpath);
        if (!get_file_location_opt(newpath)) {
            write_file_location(newpath);
        }
        rename_file_location(oldpath, newpath);
        write_response(tid, 0);
    } else {
        write_response(tid, 1);
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

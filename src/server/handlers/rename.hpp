#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP

#include "utils/location.hpp"

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath, int rank) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s, rank=%d)", tid, oldpath.c_str(),
              newpath.c_str(), rank);

    if (get_capio_file_opt(oldpath)) {
        rename_capio_file(oldpath, newpath);
        for (auto &pair : writers) {
            auto node = pair.second.extract(oldpath);
            if (!node.empty()) {
                node.key() = newpath;
                pair.second.insert(std::move(node));
            }
        }
    }
    int res = delete_from_files_location(oldpath);
    if (res != 1) {
        write_response(tid, 1);
        return;
    }
    rename_file_location(oldpath, newpath);
    write_file_location(newpath);
    write_response(tid, 0);
}

void rename_handler(const char *const str, int rank) {
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];
    int tid;
    sscanf(str, "%s %s %d", oldpath, newpath, &tid);
    handle_rename(tid, oldpath, newpath, rank);
}

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP

#include "utils/location.hpp"

void handle_rename(int tid, const char* oldpath, const char* newpath, int rank) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s, rank=%d)",
              tid, oldpath, newpath, rank);

    if (get_capio_file_opt(oldpath)) {
        rename_capio_file(oldpath, newpath);
        for (auto& pair : writers) {
            auto node = pair.second.extract(oldpath);
            if (!node.empty()) {
                node.key() = newpath;
                pair.second.insert(std::move(node));
            }
        }
    }
    int res = delete_from_file_locations("files_location_" + std::to_string(rank) + ".txt", oldpath, rank);
    if (res != 1) {
        write_response(tid, 1);
        return;
    }
    rename_file_location(oldpath, newpath);
    write_file_location(rank, newpath, tid);
    write_response(tid, 0);
}

void rename_handler(const char * const str, int rank) {
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];
    int tid;
    sscanf(str, "%s %s %d", oldpath, newpath, &tid);
    handle_rename(tid, oldpath, newpath, rank);
}

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

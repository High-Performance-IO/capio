#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP
#include "client-manager/client_manager.hpp"
#include "storage/manager.hpp"
#include "utils/location.hpp"
#include <charconv>
#include <sstream>

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s)", tid, oldpath.c_str(),
              newpath.c_str());

    // FIXME: this doesn't work if a node renames a file handled by another node
    if (storage_manager->tryGet(oldpath)) {
        storage_manager->rename(oldpath, newpath);
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
    std::string first, second, third;
    std::istringstream request(str);
    if (!(request >> first >> second >> third)) {
        return;
    }

    int tid;
    const auto parsed = std::from_chars(first.data(), first.data() + first.size(), tid);
    if (parsed.ec == std::errc{} && parsed.ptr == first.data() + first.size()) {
        return; // FS format: tid old_path new_path, no reply.
    }

    const auto tid_parsed = std::from_chars(third.data(), third.data() + third.size(), tid);
    if (tid_parsed.ec == std::errc{} && tid_parsed.ptr == third.data() + third.size()) {
        handle_rename(tid, first, second);
    }
}

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

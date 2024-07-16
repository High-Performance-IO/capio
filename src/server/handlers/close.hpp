#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "read.hpp"

#include "utils/filesystem.hpp"

inline void handle_pending_remote_nfiles(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    std::lock_guard<std::mutex> lg(nfiles_mutex);

    for (auto &p : clients_remote_pending_nfiles) {

        auto &[app, app_pending_nfiles] = p;
        LOG("Handling pending files for app: %s", app.c_str());

        for (const auto &[prefix, batch_size, dest, files_path, sem] : app_pending_nfiles) {
            LOG("Expanded iterator: prefix=%s, batch_size=%ld, dest=%s [others missing....]",
                prefix.c_str(), batch_size, dest.c_str());
            auto &files = files_sent[app];
            LOG("Obtained files for app %s", app.c_str());
            auto file_location_opt = get_file_location_opt(path);
            LOG("Handling files for prefix: %s. batch size is: %d", prefix.c_str(), batch_size);
            if (files.find(path) == files.end() && file_location_opt &&
                std::get<0>(file_location_opt->get()) == std::string(node_name) &&
                path.native().compare(0, prefix.native().length(), prefix) == 0) {
                files_path->push_back(path);
                files.insert(path);
                LOG("Inserted file %s in batch", path.c_str());
                if (files_path->size() == batch_size) {
                    LOG("Waking up thread to handle batch, as batch is full and can be served");
                    sem->unlock();
                }
            }
        }
    }
}

inline void handle_close(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    if (path.empty()) { // avoid to try to close a file that does not exists
        // (example: try to close() on a dir
        LOG("Path is empty. might be a directory. returning");
        return;
    }

    CapioFile &c_file = get_capio_file(path);
    c_file.close();
    LOG("File with path %s was closed", path.c_str());

    if (c_file.commit_rule() == CAPIO_FILE_COMMITTED_ON_CLOSE && c_file.is_closed()) {
        LOG("Capio File %s is closed and commit rule is on_close. setting it to complete and "
            "starting batch handling",
            path.c_str());
        c_file.set_complete();
        handle_pending_remote_nfiles(path);
        c_file.commit();
    }

    if (c_file.is_deletable()) {
        LOG("file %s is deletable from CAPIO_SERVER", path.c_str());
        delete_capio_file(path);
        delete_from_files_location(path);
    } else {
        LOG("Deleting capio file %s from tid=%d", path.c_str(), tid);
        delete_capio_file_from_tid(tid, fd);
    }
}

void close_handler(const char *str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_close(tid, fd);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP

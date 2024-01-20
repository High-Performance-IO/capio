#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "read.hpp"

#include "utils/filesystem.hpp"

inline void handle_close(int tid, int fd, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, rank=%d)", tid, fd, rank);

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    if (path.empty()) { // avoid to try to close a file that does not exists
        // (example: try to close() on a dir
        return;
    }

    CapioFile &c_file = get_capio_file(path);
    c_file.close();
    if (c_file.get_committed() == CAPIO_FILE_COMMITTED_ON_CLOSE && c_file.is_closed()) {
        LOG("CapioFile is closed and commit rule is on_close");
        c_file.set_complete();
        handle_pending_remote_nfiles(path);

        c_file.commit();
    }

    if (c_file.is_deletable()) {
        delete_capio_file(path);
        delete_from_file_locations(path, rank);
    } else {
        LOG("Deleting capio file from tid=%d", tid);
        delete_capio_file_from_tid(tid, fd);
    }
}

void close_handler(const char *str, int rank) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_close(tid, fd, rank);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP

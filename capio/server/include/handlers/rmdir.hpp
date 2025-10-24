#ifndef CAPIO_SERVER_HANDLERS_RMDIR_HPP
#define CAPIO_SERVER_HANDLERS_RMDIR_HPP

#include "utils/location.hpp"

inline void handle_rmdir(int tid, const std::filesystem::path &dir_to_remove) {
    START_LOG(gettid(), "call(tid=%d, dir_to_remove=%s)", tid, dir_to_remove.c_str());

    int res = delete_from_files_location(dir_to_remove);
    write_response(tid, res);
}

void rmdir_handler(const char *const str) {
    char dir_to_remove[PATH_MAX];
    int tid;
    sscanf(str, "%s %d", dir_to_remove, &tid);
    if (CapioCLEngine::get().isExcluded(dir_to_remove)) {
        write_response(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_rmdir(tid, dir_to_remove);
}

#endif // CAPIO_SERVER_HANDLERS_RMDIR_HPP

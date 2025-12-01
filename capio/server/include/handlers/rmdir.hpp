#ifndef CAPIO_SERVER_HANDLERS_RMDIR_HPP
#define CAPIO_SERVER_HANDLERS_RMDIR_HPP

#include "utils/location.hpp"

void rmdir_handler(const char *const str) {
    char dir_to_remove[PATH_MAX];
    int tid;
    sscanf(str, "%s %d", dir_to_remove, &tid);
    START_LOG(gettid(), "call(path=%s tid=%d)", dir_to_remove, tid);
    if (CapioCLEngine::get().isExcluded(dir_to_remove)) {
        write_response(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    delete_capio_file(dir_to_remove);
    int res = delete_from_files_location(dir_to_remove);
    write_response(tid, res);
}

#endif // CAPIO_SERVER_HANDLERS_RMDIR_HPP

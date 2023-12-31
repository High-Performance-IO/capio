#ifndef CAPIO_SERVER_HANDLERS_RMDIR_HPP
#define CAPIO_SERVER_HANDLERS_RMDIR_HPP

#include "utils/location.hpp"

inline void handle_rmdir(int tid, const char *dir_to_remove, int rank) {
    START_LOG(gettid(), "call(tid=%d, dir_to_remove=%s, rank=%d)", tid, dir_to_remove, rank);

    long res = delete_from_file_locations("files_location.txt", dir_to_remove, rank);
    erase_from_files_location(dir_to_remove);
    write_response(tid, res);
}

void rmdir_handler(const char *const str, int rank) {
    char dir_to_remove[PATH_MAX];
    int tid;
    sscanf(str, "%s %d", dir_to_remove, &tid);
    handle_rmdir(tid, dir_to_remove, rank);
}

#endif // CAPIO_SERVER_HANDLERS_RMDIR_HPP

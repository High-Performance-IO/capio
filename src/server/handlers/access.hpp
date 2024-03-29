#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP

#include "utils/location.hpp"

inline void handle_access(long tid, char *path) {
    START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path);

    write_response(tid, get_file_location_opt(path) ? 0 : -1);
}

void access_handler(const char *const str) {
    long tid;
    char path[PATH_MAX];
    sscanf(str, "%ld %s", &tid, path);
    handle_access(tid, path);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP

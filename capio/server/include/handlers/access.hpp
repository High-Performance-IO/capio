#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP

#include "utils/location.hpp"

void access_handler(const char *const str) {
    START_LOG(gettid(), "call(str=%s)", str);
    long tid;
    char path[PATH_MAX];
    sscanf(str, "%ld %s", &tid, path);

    if (CapioCLEngine::get().isExcluded(path)) {
        write_response(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }

    write_response(tid, get_file_location_opt(path) ? 0 : -1);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP

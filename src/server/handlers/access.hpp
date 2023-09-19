#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP

inline void handle_access(long tid, char *path) {
    START_LOG(gettid(), "call(tid=%ld, path=%s)", tid, path);

    auto it = files_location.find(path);
    if (it == files_location.end())
        write_response(tid, -1);
    else
        write_response(tid, 0);
}

void access_handler(const char * const str, int rank) {
    long tid;
    char path[PATH_MAX];
    sscanf(str, "%ld %s", &tid, path);
    handle_access(tid, path);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP

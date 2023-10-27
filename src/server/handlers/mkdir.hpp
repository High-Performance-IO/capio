#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP

inline void handle_mkdir(int tid, const char *pathname, int rank) {
    START_LOG(gettid(), "call(tid=%d, pathname=%s, rank=%d)", tid, pathname, rank);

    write_response(tid, create_dir(tid, pathname, rank, false));
}

void mkdir_handler(const char *const str, int rank) {
    pid_t tid;
    char pathname[PATH_MAX];
    sscanf(str, "%d %s", &tid, pathname);
    handle_mkdir(tid, pathname, rank);
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP

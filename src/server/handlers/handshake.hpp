#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP

inline void handle_handshake_anonymous(int tid, int pid) {
    START_LOG(gettid(), "call(tid=%d, pid=%d)", tid, pid);
    pids[tid] = pid;
    init_process(tid);
}

inline void handle_handshake_named(int tid, int pid, const char *app_name) {
    START_LOG(gettid(), "call(tid=%d, pid=%d, app_name=%s)", tid, pid, app_name);
    apps[tid] = app_name;
    pids[tid] = pid;
    init_process(tid);
}

void handshake_anonymous_handler(const char *const str, int rank) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    handle_handshake_anonymous(tid, pid);
}

void handshake_named_handler(const char *const str, int rank) {
    int tid, pid;
    char app_name[1024];
    sscanf(str, "%d %d %s", &tid, &pid, app_name);
    handle_handshake_named(tid, pid, app_name);
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP

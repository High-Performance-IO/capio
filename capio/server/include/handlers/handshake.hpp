#ifndef CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP
#define CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP

inline void handle_handshake_anonymous(int tid, int pid) {
    START_LOG(gettid(), "call(tid=%d, pid=%d)", tid, pid);
    pids[tid] = pid;
    init_process(tid);
}

/**
 * wait flag is used whenever the handshake is called after a SYS_clone occurred.
 * if wait == 1, then the server will spawn a thread waiting for the child tid to have its data
 * structures initialized
 */
inline void handle_handshake_named(int tid, int pid, const char *app_name, const bool wait) {
    START_LOG(gettid(), "call(tid=%d, pid=%d, app_name=%s)", tid, pid, app_name);
    apps[tid] = app_name;
    pids[tid] = pid;
    init_process(tid);
    if (wait) {
        std::thread t([&, target_tid = tid]() {
            do {
                {
                    std::lock_guard lock(mutex_thread_allowed_to_continue);
                    auto it = std::find(thread_allowed_to_continue.begin(),
                                        thread_allowed_to_continue.end(), target_tid);
                    if (it != thread_allowed_to_continue.end()) {
                        write_response(target_tid, 1);
                        thread_allowed_to_continue.erase(it);
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

            } while (true);
        });
        t.detach();
    }
}

void handshake_anonymous_handler(const char *const str) {
    int tid, pid;
    sscanf(str, "%d %d", &tid, &pid);
    handle_handshake_anonymous(tid, pid);
}

void handshake_named_handler(const char *const str) {
    int tid, pid, wait;
    char app_name[1024];
    sscanf(str, "%d %d %s %d", &tid, &pid, app_name, &wait);
    handle_handshake_named(tid, pid, app_name, wait);
}

#endif // CAPIO_SERVER_HANDLERS_HANDSHAKE_HPP

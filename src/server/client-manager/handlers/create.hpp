#ifndef CAPIO_CREATE_HPP
#define CAPIO_CREATE_HPP

inline void create_handler(const char *const str) {
    pid_t tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);
    file_manager->unlock_thread_awaiting_creation(path);
}

#endif // CAPIO_CREATE_HPP

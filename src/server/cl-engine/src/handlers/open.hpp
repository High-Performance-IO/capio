#ifndef OPEN_HPP
#define OPEN_HPP
inline void open_handler(const char *const str) {
    pid_t tid;
    int fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s", tid, fd, path);

    if (std::filesystem::exists(path)) {
        client_manager->reply_to_client(tid, 1);
    } else {
        client_manager->add_thread_awaiting_creation(path, tid);
    }
}
#endif // OPEN_HPP

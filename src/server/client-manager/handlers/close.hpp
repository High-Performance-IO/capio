#ifndef CAPIO_CLOSE_HPP
#define CAPIO_CLOSE_HPP

inline void close_handler(const char *const str) {
    pid_t tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    std::filesystem::path filename(path);

    if (path == get_capio_dir() || !capio_cl_engine->file_to_be_handled(filename)) {
        return;
    }

    file_manager->set_committed(path);
}

#endif // CAPIO_CLOSE_HPP

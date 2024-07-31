#ifndef WRITE_HPP
#define WRITE_HPP

inline void write_handler(const char *const str) {
    pid_t tid;
    int fd;
    capio_off64_t write_size;

    char path[PATH_MAX];
    sscanf(str, "%d %d %s %llu", &tid, &fd, path, &write_size);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, count=%llu)", tid, fd, path, write_size);
    std::filesystem::path filename(path);

    if (path == get_capio_dir() || !capio_configuration->file_to_be_handled(filename)) {
        return;
    }

    LOG("File needs to be handled");
    client_manager->unlock_thread_awaiting_data(path);
}

#endif // WRITE_HPP

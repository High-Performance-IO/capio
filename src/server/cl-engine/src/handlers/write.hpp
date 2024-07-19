#ifndef WRITE_HPP
#define WRITE_HPP

inline void write_handler(const char *const str) {
    int tid, count, fd;
    char path[PATH_MAX];
    sscanf(str, "%ld %ld %s %ld", &tid, &fd, path, &count);

    std::filesystem::path filename(path);

    if (path == get_capio_dir() || !capio_configuration->file_to_be_handled(filename)) {
        return;
    }

    client_manager->unlock_thread_awaiting_data(path);
}

#endif // WRITE_HPP

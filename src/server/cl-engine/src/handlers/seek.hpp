#ifndef CAPIO_SEEK_HPP
#define CAPIO_SEEK_HPP

inline void seek_handler(const char *const str) {
    long tid, target_offset;
    int whence, fd;
    char path[PATH_MAX];
    sscanf(str, "%ld %s %ld %d %d", &tid, path, &target_offset, &fd, &whence);
    START_LOG(gettid(), "call()");
    std::filesystem::path filename(path);

    // TODO: MIGHT NOT BE NEEDED
    if (path == get_capio_dir() || !capio_configuration->file_to_be_handled(filename)) {
        return;
    }
    auto path_size = std::filesystem::file_size(path);
    if (path_size >= target_offset) {
        client_manager->reply_to_client(tid, path_size);
    } else {
    }
}

#endif // CAPIO_SEEK_HPP

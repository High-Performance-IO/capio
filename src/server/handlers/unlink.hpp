#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP

inline void handle_unlink(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    auto c_file_opt           = get_capio_file_opt(path);
    std::filesystem::path tmp = path;
    backend->notify_backend(Backend::deleteFile, tmp, nullptr, 0, 0, false);

    if (c_file_opt) { // TODO: it works only in the local case
        CapioFile &c_file = c_file_opt->get();
        c_file.unlink();
        if (c_file.is_deletable()) {
            delete_capio_file(path);
            delete_from_files_location(path);
        }
        write_response(tid, 0);
    } else {
        write_response(tid, -1);
    }
}

void unlink_handler(const char *const str) {
    char path[PATH_MAX];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    handle_unlink(tid, path);
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP

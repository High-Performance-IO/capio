#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP

inline void handle_unlink(int tid, const char *path, int rank) {
    START_LOG(gettid(), "call(tid=%d, path=%s, rank=%d)", tid, path, rank);

    auto c_file_opt = get_capio_file_opt(path);
    if (c_file_opt) { // TODO: it works only in the local case
        Capio_file &c_file = c_file_opt->get();
        c_file.unlink();
        if (c_file.is_deletable()) {
            delete_capio_file(path);
            delete_from_file_locations(path, rank);
        }
        write_response(tid, 0);
    } else {
        write_response(tid, -1);
    }
}

void unlink_handler(const char *const str, int rank) {
    char path[PATH_MAX];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    handle_unlink(tid, path, rank);
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP

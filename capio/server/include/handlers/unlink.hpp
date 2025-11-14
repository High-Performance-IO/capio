#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP

void unlink_handler(const char *const str) {
    char path[PATH_MAX];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        write_response(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    auto c_file_opt = get_capio_file_opt(path);
    if (c_file_opt) { // TODO: it works only in the local case
        CapioFile &c_file = c_file_opt->get();
        if (c_file.is_deletable()) {
            delete_capio_file(path);
            delete_from_files_location(path);
        }
        write_response(tid, 0);
    } else {
        write_response(tid, -1);
    }
}

#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP

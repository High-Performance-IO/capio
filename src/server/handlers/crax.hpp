#ifndef CAPIO_SERVER_HANDLERS_CRAX_HPP
#define CAPIO_SERVER_HANDLERS_CRAX_HPP

void handle_crax(const char* str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    off64_t res = 1;
    sscanf(str, "crax %d %d %s", &tid, &fd, path_cstr);
    std::string path(path_cstr);
    init_process(tid);
    if (files_metadata.find(path) == files_metadata.end()) {
        res = 0;
        response_buffers[tid]->write(&res);
        update_file_metadata(path, tid, fd, rank, true);
    }
    else
        response_buffers[tid]->write(&res);
}

#endif // CAPIO_SERVER_HANDLERS_CRAX_HPP

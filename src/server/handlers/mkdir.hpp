#ifndef CAPIO_SERVER_HANDLERS_MKDIR_HPP
#define CAPIO_SERVER_HANDLERS_MKDIR_HPP

void handle_mkdir(const char* str, int rank) {
    pid_t tid;
    char pathname[PATH_MAX];
    sscanf(str, "mkdi %d %s", &tid, pathname);
    init_process(tid);
    off64_t res = create_dir(tid, pathname, rank, false);
    response_buffers[tid]->write(&res);
}

#endif // CAPIO_SERVER_HANDLERS_MKDIR_HPP

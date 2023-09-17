#ifndef CAPIO_SERVER_HANDLERS_ACCESS_HPP
#define CAPIO_SERVER_HANDLERS_ACCESS_HPP

void handle_access(const char* str) {
    int tid;
    char path[PATH_MAX];
#ifdef CAPIOLOG
    logfile << "handle access: " << str << std::endl;
#endif
    sscanf(str, "accs %d %s", &tid, path);
    off64_t res;
    auto it = files_location.find(path);
    if (it == files_location.end())
        res = -1;
    else
        res = 0;
#ifdef CAPIOLOG
    logfile << "handle access result: " << res << std::endl;
#endif
    response_buffers[tid]->write(&res);
}

#endif // CAPIO_SERVER_HANDLERS_ACCESS_HPP

#ifndef CAPIO_OPEN_HPP
#define CAPIO_OPEN_HPP

void handle_open(char* str, int rank, bool is_creat) {
#ifdef CAPIOLOG
    logfile << "handle open" << std::endl;
#endif
    int tid, fd;
    char path_cstr[PATH_MAX];
    if (is_creat)
        sscanf(str, "crat %d %d %s", &tid, &fd, path_cstr);
    else
        sscanf(str, "open %d %d %s", &tid, &fd, path_cstr);
    init_process(tid);
    update_file_metadata(path_cstr, tid, fd, rank, is_creat);
}


#endif //CAPIO_OPEN_HPP

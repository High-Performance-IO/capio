#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

void handle_open(char* str, int rank, bool is_creat) {
#ifdef CAPIOLOG
    logfile << "handle open" << std::endl;
#endif
    int tid, fd;
    char path_cstr[PATH_MAX];
    off64_t res = 0;
    if (is_creat) {
        sscanf(str, "crat %d %d %s", &tid, &fd, path_cstr);
        init_process(tid);
        if (files_location.find(path_cstr) != files_location.end() || check_file_location(rank, path_cstr))
            is_creat = false;
        update_file_metadata(path_cstr, tid, fd, rank, is_creat);
    }
    else {
        sscanf(str, "open %d %d %s", &tid, &fd, path_cstr);
        init_process(tid);
        //it is important that check_files_location is the last beacuse is the slowest (short circuit evalutation)
        if (files_location.find(path_cstr) != files_location.end() || metadata_conf.find(path_cstr) != metadata_conf.end() || match_globs(path_cstr, &metadata_conf_globs) != -1 || check_file_location(rank, path_cstr)) {
            update_file_metadata(path_cstr, tid, fd, rank, is_creat);
#ifdef CAPIOLOG
            logfile << "file found" << std::endl;
#endif
        }
        else
            res = 1;

    }
    response_buffers[tid]->write(&res);
}

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

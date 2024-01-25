#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

inline void handle_exit_group(int tid) {
    START_LOG(gettid(), "call(tid=%d)", tid);

    LOG("retrieving pid for process with tid = %d", tid);
    int pid = pids[tid];
    LOG("retrieving files from writers for process with pid = %d", pid);
    auto files = writers[pid];
    for (auto &pair : files) {
        std::string path = pair.first;
        LOG("Path %s found. handling? %s", path.c_str(), pair.second ? "yes" : "no");
        if (pair.second) {
            LOG("Handling file %s", path.c_str());
            auto it_conf = metadata_conf.find(path);
            if (it_conf == metadata_conf.end() ||
                std::get<0>(it_conf->second) == CAPIO_FILE_COMMITTED_ON_TERMINATION ||
                std::get<0>(it_conf->second).empty()) {
                CapioFile &c_file = get_capio_file(path.c_str());
                if (c_file.is_dir()) {
                    LOG("file %s is dir", path.c_str());
                    long int n_committed = c_file.n_files_expected;
                    if (n_committed <= c_file.n_files) {
                        LOG("Setting file %s to complete", path.c_str());
                        c_file.set_complete();
                    }
                } else {
                    LOG("Setting file %s to complete", path.c_str());
                    c_file.set_complete();
                    c_file.commit();
                }
            }
        }
    }

    for (auto &fd : get_capio_fds_for_tid(tid)) {
        handle_close(tid, fd);
    }
    free_resources(tid);
}

void exit_group_handler(const char *const str, int rank) {
    int tid;
    sscanf(str, "%d", &tid);
    handle_exit_group(tid);
}

#endif // CAPIO_SERVER_HANDLERS_EXITG_HPP

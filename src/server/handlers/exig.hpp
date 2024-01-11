#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

inline void handle_exit_group(int tid, int rank) {
    START_LOG(gettid(), "call(tid=%d, rank=%d)", tid, rank);

    int pid    = pids[tid];
    auto files = writers[pid];
    for (auto &pair : files) {
        std::string path = pair.first;
        if (pair.second) {
            LOG("Handling file %s", path.c_str());
            auto it_conf = metadata_conf.find(path);
            if (it_conf == metadata_conf.end() ||
                std::get<0>(it_conf->second) == CAPIO_FILE_MODE_ON_TERMINATION ||
                std::get<0>(it_conf->second).empty()) {
                Capio_file &c_file = get_capio_file(path.c_str());
                if (c_file.is_dir()) {
                    LOG("file %s is dir", path.c_str());
                    long int n_committed = c_file.n_files_expected;
                    if (n_committed <= c_file.n_files) {
                        reply_remote_stats(path);
                        LOG("Setting file %s to complete", path.c_str());
                        c_file.set_complete();
                    }
                } else {
                    LOG("Setting file %s to complete", path.c_str());
                    c_file.set_complete();
                    c_file.commit();
                }
            }

            auto it = pending_reads.find(path);
            if (it != pending_reads.end()) {
                LOG("Handling pending reads for file %s", path.c_str());
                auto &pending_reads_this_file = it->second;
                auto it_vec                   = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                    auto &[pending_tid, fd, count, is_getdents] = *it_vec;
                    size_t process_offset                       = get_capio_file_offset(tid, fd);
                    handle_pending_read(pending_tid, fd, process_offset, count, is_getdents);
                    it_vec = pending_reads_this_file.erase(it_vec);
                }
                pending_reads.erase(it);
            }
        }
    }
    for (auto &fd : get_capio_fds_for_tid(tid)) {
        handle_close(tid, fd, rank);
    }
    free_resources(tid);
}

void exit_group_handler(const char *const str, int rank) {
    int tid;
    sscanf(str, "%d", &tid);
    handle_exit_group(tid, rank);
}

#endif // CAPIO_SERVER_HANDLERS_EXITG_HPP

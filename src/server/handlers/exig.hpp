#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

void close_all_files(int tid, int rank) {
    START_LOG(tid, "call(%d, %d)", tid, rank);
    auto it_process_files = processes_files.find(tid);
    if (it_process_files != processes_files.end()) {
        auto process_files = it_process_files->second;
        for (auto it : process_files) {
            handle_close(tid, it.first, rank);
        }
    }

}

inline void handle_exit_group(int tid, int rank) {
    START_LOG(gettid(), "call(tid=%d, rank=%d)", tid, rank);

    int pid = pids[tid];
    auto files = writers[pid];
    for (auto& pair : files) {
        std::string path = pair.first;
        if (pair.second) {
            auto it_conf = metadata_conf.find(path);
            if (it_conf == metadata_conf.end() || std::get<0>(it_conf->second) == "on_termination" || std::get<0>(it_conf->second).length() == 0) {
                Capio_file& c_file = get_capio_file(path.c_str());
                if (c_file.is_dir()) {
                    long int n_committed = c_file.n_files_expected;
                    if (n_committed <= c_file.n_files) {
                        reply_remote_stats(path);
                        c_file.complete = true;
                    }
                }
                else {
                    c_file.complete = true;
                    c_file.commit();
                }
            }

            auto it = pending_reads.find(path);
            if (it != pending_reads.end()) {


                auto& pending_reads_this_file = it->second;
                auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                    auto tuple = *it_vec;
                    int pending_tid = std::get<0>(tuple);
                    int fd = std::get<1>(tuple);
                    size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                    size_t count = std::get<2>(tuple);

                    handle_pending_read(pending_tid, fd, process_offset, count, std::get<3>(tuple));
                    it_vec = pending_reads_this_file.erase(it_vec);
                }
                pending_reads.erase(it);
            }
        }
    }

    close_all_files(tid, rank);
    free_resources(tid);
}

void exit_group_handler(const char * const str, int rank) {
    int tid;
    sscanf(str, "%d", &tid);
    handle_exit_group(tid, rank);
}

#endif // CAPIO_SERVER_HANDLERS_EXITG_HPP

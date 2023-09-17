#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

void close_all_files(int tid, int rank) {
    auto it_process_files = processes_files.find(tid);
    if (it_process_files != processes_files.end()) {
        auto process_files = it_process_files->second;
        for (auto it : process_files) {
            handle_close(tid, it.first, rank);
        }
    }
}

void handle_exig(char* str, int rank) {
    int tid;
    sscanf(str, "exig %d", &tid);
#ifdef CAPIOLOG
    logfile << "handle exit group " << std::endl;
#endif
    int pid = pids[tid];
    auto files = writers[pid];
    for (auto& pair : files) {
        std::string path = pair.first;
        if (pair.second) {
            auto it_conf = metadata_conf.find(path);
#ifdef CAPIOLOG
            logfile << "path: " << path << std::endl;
#endif
            if (it_conf == metadata_conf.end() || std::get<0>(it_conf->second) == "on_termination" || std::get<0>(it_conf->second).length() == 0) {
                sem_wait(&files_metadata_sem);
                Capio_file& c_file = *files_metadata[path];
                sem_post(&files_metadata_sem);
#ifdef CAPIOLOG
                logfile << "committed " << c_file.get_committed() << std::endl;
#endif
                if (c_file.is_dir()) {
                    long int n_committed = c_file.n_files_expected;
#ifdef CAPIOLOG
                    logfile << "nfiles in dir " << path << " " << c_file.n_files << std::endl;
#endif
                    if (n_committed <= c_file.n_files) {
#ifdef CAPIOLOG
                        logfile << "dir " << path << " completed " << std::endl;
#endif
                        reply_remote_stats(path);
                        c_file.complete = true;
                    }
                }
                else {
#ifdef CAPIOLOG
                    logfile << "file " << path << " completed" << std::endl;
#endif
                    c_file.complete = true;
                    c_file.commit();
                }
            }
            else {
#ifdef CAPIOLOG
                logfile << "committed " << std::get<0>(it_conf->second) << " mode " << std::get<1>(it_conf->second) << std::endl;
#endif
            }
            auto it = pending_reads.find(path);
            if (it != pending_reads.end()) {
#ifdef CAPIOLOG
                logfile << "handle pending read file on_termination " << path << std::endl;
#endif
                auto& pending_reads_this_file = it->second;
                auto it_vec = pending_reads_this_file.begin();
                while (it_vec != pending_reads_this_file.end()) {
                    auto tuple = *it_vec;
                    int pending_tid = std::get<0>(tuple);
                    int fd = std::get<1>(tuple);
                    size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                    size_t count = std::get<2>(tuple);
#ifdef CAPIOLOG
                    logfile << "pending read tid fd offset count " << tid << " " << fd << " " << process_offset <<" "<< count << std::endl;
#endif
                    handle_pending_read(pending_tid, fd, process_offset, count, std::get<3>(tuple));
                    it_vec = pending_reads_this_file.erase(it_vec);
                }
                pending_reads.erase(it);
            }
        }
    }
#ifdef CAPIOLOG
    logfile << "handle exit group 3" << std::endl;
#endif
    close_all_files(tid, rank);
    free_resources(tid);
}

#endif // CAPIO_SERVER_HANDLERS_EXITG_HPP

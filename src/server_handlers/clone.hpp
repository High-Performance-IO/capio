#ifndef CAPIO_CLONE_HPP
#define CAPIO_CLONE_HPP
//TODO: caching info
void handle_clone(const char* str) {
    pid_t ptid, new_tid;
    sscanf(str, "clon %d %d", &ptid, &new_tid);
    init_process(new_tid);
    processes_files[new_tid] = processes_files[ptid];
    processes_files_metadata[new_tid] = processes_files_metadata[ptid];
    int ppid = pids[ptid];
    int new_pid = pids[new_tid];
    if (ppid != new_pid) {
        writers[new_tid] = writers[ptid];
        for (auto &p : writers[new_tid]) {
            p.second = false;
        }
    }
    std::unordered_set<std::string> parent_files = get_paths_opened_files(ptid);
    for(std::string path : parent_files) {
        sem_wait(&files_metadata_sem);
        Capio_file& c_file = *files_metadata[path];
        sem_post(&files_metadata_sem);
        ++c_file.n_opens;
    }
}

#endif //CAPIO_CLONE_HPP

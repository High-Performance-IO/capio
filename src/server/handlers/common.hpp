#ifndef CAPIO_SERVER_HANDLERS_COMMON_HPP
#define CAPIO_SERVER_HANDLERS_COMMON_HPP

inline void init_process(int tid) {
    START_LOG(tid, "call(%d)", tid);

    if (data_buffers.find(tid) == data_buffers.end()) {
        register_listener(tid);

        auto *write_data_cb = new SPSC_queue<char>(
                "capio_write_data_buffer_tid_" + std::to_string(tid),
                N_ELEMS_DATA_BUFS,
                WINDOW_DATA_BUFS,
                CAPIO_SEM_TIMEOUT_NANOSEC,
                CAPIO_SEM_RETRIES);
        auto *read_data_cb = new SPSC_queue<char>(
                "capio_read_data_buffer_tid_" + std::to_string(tid),
                N_ELEMS_DATA_BUFS,
                WINDOW_DATA_BUFS,
                CAPIO_SEM_TIMEOUT_NANOSEC,
                CAPIO_SEM_RETRIES);
        data_buffers.insert({tid, {write_data_cb, read_data_cb}});
    }
}

void send_data_to_client(int tid, char *buf, long int count) {
    START_LOG(tid ,"call(%d,%s, %ld)", tid, buf, count);
    auto *data_buf = data_buffers[tid].second;
    size_t n_writes = count / WINDOW_DATA_BUFS;
    size_t r = count % WINDOW_DATA_BUFS;
    size_t i = 0;
    while (i < n_writes) {
        data_buf->write(buf + i * WINDOW_DATA_BUFS);
        ++i;
    }
    if (r)
        data_buf->write(buf + i * WINDOW_DATA_BUFS, r);
}


/*
 * Unlink resources in shared memory of the thread with thread id = tid
 * To be called only when the client thread terminates
 */

void free_resources(int tid) {
    START_LOG(tid, "call(%d)", tid);
    std::string sem_write_shm_name;
    remove_listener(tid);

    auto it = data_buffers.find(tid);
    if (it != data_buffers.end()) {
        it->second.first->free_shm();
        it->second.second->free_shm();
        data_buffers.erase(it);
    }

}

void handle_pending_remote_nfiles(const std::string& path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    if (sem_wait(&clients_remote_pending_nfiles_sem) == -1)
        ERR_EXIT("sem_wait clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles");
    for (auto &p: clients_remote_pending_nfiles) {
        std::string app = p.first;
        auto &app_pending_nfiles = p.second;
        auto it = app_pending_nfiles.begin();
        while (it != app_pending_nfiles.end()) {
            auto& [prefix, n_files, dest, files_path, sem] = *it;
            std::unordered_set<std::string> &files = files_sent[app];
            if (sem_wait(&files_location_sem) == -1)
                ERR_EXIT("sem_wait files_location_sem in handle_pending_remote_nfiles");
            auto it_fs = files_location.find(path);
            auto next_it = std::next(it);
            if (files.find(path) == files.end() && it_fs != files_location.end() &&
                strcmp(std::get<0>(it_fs->second), node_name) == 0 && path.compare(0, strlen(prefix), prefix) == 0) {
                if (sem_post(&files_location_sem) == -1)
                    ERR_EXIT("sem_post files_location_sem in handle_pending_remote_nfiles");
                files_path->push_back(path);
                files.insert(path);
                if (files_path->size() == n_files) {
                    app_pending_nfiles.erase(it);
                    if (sem_post(sem) == -1)
                        ERR_EXIT("sem_post sem in handle_pending_remote_nfiles");
                }
            } else {
                if (sem_post(&files_location_sem) == -1)
                    ERR_EXIT("sem_post files_location_sem in handle_pending_remote_nfiles");
            }
            it = next_it;
        }
    }
    if (sem_post(&clients_remote_pending_nfiles_sem) == -1)
        ERR_EXIT("sem_post clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles");
}


void open_files_metadata(int rank, int *fd_files_location) {
    START_LOG(gettid(), "call(%d)", rank);
    std::string rank_str = std::to_string(rank);
    std::string file_name = "files_location_" + rank_str + ".txt";
    int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0664)) == -1) {
        MPI_Finalize();
        ERR_EXIT("writer error opening file");
    }
    *fd_files_location = fd;
}

#endif // CAPIO_SERVER_HANDLERS_COMMON_HPP

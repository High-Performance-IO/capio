#ifndef CAPIO_SERVER_HANDLERS_COMMON_HPP
#define CAPIO_SERVER_HANDLERS_COMMON_HPP

inline void init_process(int tid) {
    START_LOG(gettid(), "call(%d)", tid);

    if (data_buffers.find(tid) == data_buffers.end()) {
        register_listener(tid);

        auto *write_data_cb = new SPSCQueue<char>(
            "capio_write_data_buffer_tid_" + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
            CAPIO_DATA_BUFFER_ELEMENT_SIZE, CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES,
            workflow_name.data());
        auto *read_data_cb = new SPSCQueue<char>(
            "capio_read_data_buffer_tid_" + std::to_string(tid), CAPIO_DATA_BUFFER_LENGTH,
            CAPIO_DATA_BUFFER_ELEMENT_SIZE, CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES,
            workflow_name.data());
        data_buffers.insert({tid, {write_data_cb, read_data_cb}});
    }
}

void send_data_to_client(int tid, char *buf, long int count) {
    START_LOG(gettid(), "call(%d,%.10s, %ld)", tid, buf, count);
    auto *data_buf  = data_buffers[tid].second;
    size_t n_writes = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t r        = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    size_t i        = 0;
    while (i < n_writes) {
        data_buf->write(buf + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
        ++i;
    }
    if (r) {
        data_buf->write(buf + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE, r);
    }
}

/*
 * Unlink resources in shared memory of the thread with thread id = tid
 * To be called only when the client thread terminates
 */

void free_resources(int tid) {
    START_LOG(gettid(), "call(%d)", tid);
    std::string sem_write_shm_name;
    remove_listener(tid);

    auto it = data_buffers.find(tid);
    if (it != data_buffers.end()) {
        it->second.first->free_shm();
        it->second.second->free_shm();
        data_buffers.erase(it);
    }
}

void handle_pending_remote_nfiles(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    if (sem_wait(&clients_remote_pending_nfiles_sem) == -1) {
        ERR_EXIT("sem_wait clients_remote_pending_nfiles_sem in "
                 "handle_pending_remote_nfiles");
    }
    for (auto &p : clients_remote_pending_nfiles) {
        std::string app          = p.first;
        auto &app_pending_nfiles = p.second;
        auto it                  = app_pending_nfiles.begin();
        while (it != app_pending_nfiles.end()) {
            auto &[prefix, batch_size, dest, files_path, sem] = *it;
            std::unordered_set<std::string> &files            = files_sent[app];
            auto file_location_opt                            = get_file_location_opt(path);
            auto next_it                                      = std::next(it);
            if (files.find(path) == files.end() && file_location_opt &&
                strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 &&
                path.native().compare(0, prefix.native().length(), prefix) == 0) {
                files_path->push_back(path);
                files.insert(path);
                if (files_path->size() == batch_size) {
                    app_pending_nfiles.erase(it);
                    // wake wait_for_nfiles
                    if (sem_post(sem) == -1) {
                        ERR_EXIT("sem_post sem in "
                                 "handle_pending_remote_nfiles");
                    }
                }
            }
            it = next_it;
        }
    }
    if (sem_post(&clients_remote_pending_nfiles_sem) == -1) {
        ERR_EXIT("sem_post clients_remote_pending_nfiles_sem in "
                 "handle_pending_remote_nfiles");
    }
}

#endif // CAPIO_SERVER_HANDLERS_COMMON_HPP

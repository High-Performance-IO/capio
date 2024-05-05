#ifndef CAPIO_SERVER_HANDLERS_COMMON_HPP
#define CAPIO_SERVER_HANDLERS_COMMON_HPP

inline void init_process(int tid) {
    START_LOG(gettid(), "call(%d)", tid);

    if (data_buffers.find(tid) == data_buffers.end()) {
        register_listener(tid);

        data_buffers.insert(
            {tid,
             {new SPSCQueue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), get_cache_lines(),
                            get_cache_line_size(), workflow_name),
              new SPSCQueue(SHM_SPSC_PREFIX_READ + std::to_string(tid), get_cache_lines(),
                            get_cache_line_size(), workflow_name)}});
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
        delete it->second.first;
        delete it->second.second;
        data_buffers.erase(it);
    }
}

void handle_pending_remote_nfiles(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    std::lock_guard<std::mutex> lg(nfiles_mutex);

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
                    sem->unlock();
                }
            }
            it = next_it;
        }
    }
}

#endif // CAPIO_SERVER_HANDLERS_COMMON_HPP

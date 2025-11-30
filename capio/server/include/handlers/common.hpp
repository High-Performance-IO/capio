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

inline void free_resources(const int tid) {
    START_LOG(gettid(), "call(%d)", tid);
    std::string sem_write_shm_name;
    remove_listener(tid);

    if (const auto it = data_buffers.find(tid); it != data_buffers.end()) {
        delete it->second.first;
        delete it->second.second;
        data_buffers.erase(it);
    }
}

#endif // CAPIO_SERVER_HANDLERS_COMMON_HPP

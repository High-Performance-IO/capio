#ifndef CONSENT_HPP
#define CONSENT_HPP
/*
This handler only checks if the client is allowed to continue
*/

inline void consent_to_proceed_handler(const char *const str) {
    pid_t tid;
    char path[1024], source_func[1024];
    sscanf(str, "%d %s %s", &tid, path, source_func);
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    std::filesystem::path path_fs(path);

    // Skip operations on CAPIO_DIR
    // TODO: check if it is coherent with CAPIO_CL
    if (path_fs == get_capio_dir()) {
        LOG("Ignore calls on exactly CAPIO_DIR");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (!capio_cl_engine->file_to_be_handled(path_fs)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (std::filesystem::exists(path) || CapioFileManager::is_committed(path)) {
        LOG("It is possible to unlokc waiting thread");
        client_manager->reply_to_client(tid, 1);
    } else {
        LOG("Requested file %s does not exists yet. awaiting for creation", path);
        file_manager->add_thread_awaiting_creation(path, tid);
    }
}

#endif // CONSENT_HPP

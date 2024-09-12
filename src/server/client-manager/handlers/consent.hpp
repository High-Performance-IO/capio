#ifndef CONSENT_HPP
#define CONSENT_HPP
/*
This handler only checks if the client is allowed to continue
*/

inline void consent_to_proceed_handler(const char *const str) {
    pid_t tid;
    char path[1024], source_func[1024];
    sscanf(str, "%d %s %s", &tid, path, source_func);
    START_LOG(gettid(), "call(tid=%d, path=%s, source=%s)", tid, path, source_func);

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

    // TODO: check this expression as being the correct evaluation one
    // NOTE: expression is (exists AND (committed OR no_update)) OR is_producer

    bool exists      = std::filesystem::exists(path);
    bool committed   = CapioFileManager::is_committed(path);
    bool firable     = capio_cl_engine->getFireRule(path) == CAPIO_FILE_MODE_NO_UPDATE;
    bool is_producer = capio_cl_engine->isProducer(path, tid);
    LOG("exists=%s, committed=%s, firable=%s, is_producer=%s", exists ? "true" : "false",
        committed ? "true" : "false", firable ? "true" : "false", is_producer ? "true" : "false");
    if ((exists && (committed || firable)) || is_producer) {
        LOG("It is possible to unlock waiting thread");
        client_manager->reply_to_client(tid, 1);
    } else {
        LOG("Requested file %s does not exists yet. awaiting for creation", path);
        file_manager->add_thread_awaiting_data(path, tid, 0);
    }
}

#endif // CONSENT_HPP

#ifndef CONSENT_HPP
#define CONSENT_HPP

/**
 * @brief Handle the consent to proceed request. This handler only checks whether the conditions for
 * a systemcall to continue are met.
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void consent_to_proceed_handler(const char *const str) {
    pid_t tid;
    char path[1024], source_func[1024];
    sscanf(str, "%d %s %s", &tid, path, source_func);
    START_LOG(gettid(), "call(tid=%d, path=%s, source=%s)", tid, path, source_func);

    std::filesystem::path path_fs(path);

    // Skip operations on CAPIO_DIR
    if (!CapioCLEngine::fileToBeHandled(path_fs)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }
    if (capio_cl_engine->isProducer(path, tid)) {
        LOG("Application is producer. continuing");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    // TODO: check this expression as being the correct evaluation one
    // NOTE: expression is (exists AND (committed OR no_update))

    bool exists    = std::filesystem::exists(path);
    bool committed = CapioFileManager::isCommitted(path);
    bool firable   = capio_cl_engine->getFireRule(path) == CAPIO_FILE_MODE_NO_UPDATE;

    LOG("exists=%s, committed=%s, firable=%s", exists ? "true" : "false",
        committed ? "true" : "false", firable ? "true" : "false");

    if (exists && (committed || firable)) {
        LOG("It is possible to unlock waiting thread");
        client_manager->reply_to_client(tid, 1);
    } else {
        LOG("Requested file %s does not exists yet. awaiting for creation", path);
        file_manager->addThreadAwaitingData(path, tid, 0);
    }
}

#endif // CONSENT_HPP

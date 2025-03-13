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

    // Skip operations on CAPIO_DIR
    if (!CapioCLEngine::fileToBeHandled(path) || !capio_cl_engine->contains(path)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (capio_cl_engine->isProducer(path, tid)) {
        LOG("Application is producer. continuing");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (!std::filesystem::exists(path)) {
        LOG("Requested file %s does not exists yet. awaiting for creation", path);
        file_manager->addThreadAwaitingCreation(path, tid);
        return;
    }

    if (capio_cl_engine->isFirable(path)) {
        LOG("Mode for file %s is no_update. allowing process to continue", path);
        client_manager->reply_to_client(tid, 1);
        return;
    }

    if (CapioFileManager::isCommitted(path)) {
        LOG("It is possible to unlock waiting thread");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    LOG("File %s is not yet committed. Adding to threads waiting for committed with  ULLONG_MAX",
        path);
    file_manager->addThreadAwaitingData(path, tid, ULLONG_MAX);
}

#endif // CONSENT_HPP

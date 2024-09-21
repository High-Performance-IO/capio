#ifndef CAPIO_CLOSE_HPP
#define CAPIO_CLOSE_HPP

inline void close_handler(const char *const str) {
    pid_t tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    std::filesystem::path filename(path);

    if (!CapioCLEngine::fileToBeHandled(filename)) {
        LOG("File should not be handled");
        return;
    }

    LOG("File needs handling");

    // Call the set_committed method only if the commit rule is on_close
    if (capio_cl_engine->getCommitRule(filename) == CAPIO_FILE_COMMITTED_ON_CLOSE) {
        file_manager->setCommitted(path);

        // The increase close count is called only on explicit close() sc, as defined by the
        // CAPIO-CL specification. If it were to be called every time the file is committed, then
        // an extra increase would occur as by default, at termination all files are committed.
        // By calling this only when close sc are occurred, we guarantee the correct count of
        // how many close sc occurs.
        CapioFileManager::increaseCloseCount(path);
    }
}

#endif // CAPIO_CLOSE_HPP

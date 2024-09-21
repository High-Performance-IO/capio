#ifndef READ_HPP
#define READ_HPP

inline void read_handler(const char *const str) {
    pid_t tid;
    int fd;
    capio_off64_t end_of_read;
    char path[PATH_MAX];

    sscanf(str, "%s %d %d %llu", path, &tid, &fd, &end_of_read);
    START_LOG(gettid(), "call(path=%s, tid=%ld, end_of_read=%llu)", path, tid, end_of_read);

    std::filesystem::path path_fs(path);
    // Skip operations on CAPIO_DIR
    if (!CapioCLEngine::fileToBeHandled(path_fs)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    auto is_committed = CapioFileManager::isCommitted(path);
    auto file_size    = std::filesystem::file_size(path);

    // return ULLONG_MAX to signal client cache that file is committed and no more requests are
    // required
    if (file_size >= end_of_read || is_committed || capio_cl_engine->isProducer(path, tid)) {
        client_manager->reply_to_client(tid, is_committed ? ULLONG_MAX : file_size);
    } else {
        file_manager->addThreadAwaitingData(path, tid, end_of_read);
    }
}

#endif // READ_HPP

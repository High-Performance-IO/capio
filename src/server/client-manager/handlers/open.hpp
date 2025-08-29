#ifndef OPEN_HPP
#define OPEN_HPP

/**
 * @brief Handle the open systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void open_handler(const char *const str) {
    pid_t tid;
    int fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s", tid, fd, path);

    if (capio_cl_engine->isProducer(path, tid)) {
        LOG("Thread is producer. allowing to continue with open");
        client_manager->reply_to_client(tid, 1);
        storage_service->createMemoryFile(path);
        return;
    }

    if (std::filesystem::exists(path)) {
        LOG("File already exists! allowing to continue with open");
        client_manager->reply_to_client(tid, 1);

        /*
         * At this point, the file that needs to be created more likely than not is not local to the
         * machine. As such, we call the creation of a new CapioRemoteFile
         */
        storage_service->createRemoteFile(path);
        return;
    }

    LOG("File does not yet exists. halting operation and adding it to queue");
    file_manager->addThreadAwaitingCreation(path, tid);
}
#endif // OPEN_HPP
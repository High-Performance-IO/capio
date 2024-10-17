#ifndef READ_HPP
#define READ_HPP
#include "file-manager/file_manager_impl.hpp"

/**
 * @brief Handle the read systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void read_handler(const char *const str) {
    pid_t tid;
    int fd;
    capio_off64_t end_of_read;
    char path[PATH_MAX];

    sscanf(str, "%s %d %d %llu", path, &tid, &fd, &end_of_read);
    START_LOG(gettid(), "call(path=%s, tid=%ld, end_of_read=%llu)", path, tid, end_of_read);

    const std::filesystem::path path_fs(path);
    // Skip operations on CAPIO_DIR
    if (!CapioCLEngine::fileToBeHandled(path_fs)) {
        LOG("Ignore calls as file should not be treated by CAPIO");
        client_manager->reply_to_client(tid, 1);
        return;
    }

    /**
     * If process is producer OR fire rule is no update and there is enough data, allow the process
     * to continue in its execution
     */
    const uintmax_t file_size = CapioFileManager::get_file_size_if_exists(path);
    if (capio_cl_engine->isProducer(path, tid) ||
        (capio_cl_engine->getFireRule(path_fs) == CAPIO_FILE_MODE_NO_UPDATE &&
         file_size >= end_of_read)) {
        LOG("File can be consumed as it is either the producer, or the fire rule is FNU and there "
            "is enough data");
        client_manager->reply_to_client(tid, file_size);
        return;
    }

    /**
     * return ULLONG_MAX to signal client cache that file is committed and no more requests are
     * required
     */
    if (CapioFileManager::isCommitted(path)) {
        LOG("File is committed, and hence can be consumed");
        client_manager->reply_to_client(tid, ULLONG_MAX);
        return;
    }

    /**
     * Otherwise, file cannot yet be consumed, and hence add thread to wait for data...
     */
    LOG("File cannot yet be consumed. Adding thread to wait list");
    file_manager->addThreadAwaitingData(path, tid, end_of_read);
}

#endif // READ_HPP

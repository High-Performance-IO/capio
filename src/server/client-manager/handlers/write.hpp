#ifndef WRITE_HPP
#define WRITE_HPP
#include "capio-cl-engine/capio_cl_engine.hpp"

/**
 * @brief Handle the write systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
inline void write_handler(const char *const str) {
    pid_t tid;
    int fd;
    capio_off64_t write_size;

    char path[PATH_MAX];
    sscanf(str, "%d %d %s %llu", &tid, &fd, path, &write_size);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, count=%llu)", tid, fd, path, write_size);
    std::filesystem::path filename(path);

    if (!CapioCLEngine::fileToBeHandled(filename)) {
        return;
    }

    /**
     * File needs to be handled, however, to not overload the client manager thread, on which
     * this function is being called, the function checkAndUnlockThreadAwaitingData is not
     * being called, as worst case scenario after 300ms the FSMonitor thread will catch the
     * occurrence of the write and handle it. TODO: possibly find a low overhead way to improve on
     * this
     */
    LOG("File needs to be handled");
    // file_manager->checkAndUnlockThreadAwaitingData(path);
}

#endif // WRITE_HPP

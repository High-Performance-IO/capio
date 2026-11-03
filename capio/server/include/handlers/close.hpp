#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "read.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

/**
 * Whether @tid produces @path: CAPIO-CL names its application as the producer, or the process, or
 * another of its application, created the file. The same test the read handler uses.
 */
inline bool is_producer_of(int tid, const std::filesystem::path &path) {
    return CapioCLEngine::get().isProducer(path, client_manager->getAppName(tid)) ||
           client_manager->isProducer(tid, path);
}

inline void handle_close(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::filesystem::path path = storage_manager->getPath(tid, fd);
    if (path.empty()) { // avoid to try to close a file that does not exists
        // (example: try to close() on a dir
        LOG("Path is empty. might be a directory. returning");
        return;
    }

    CapioFile &c_file = storage_manager->get(tid, fd);
    c_file.close();
    LOG("File was closed", path.c_str());

    const bool is_prod = is_producer_of(tid, path);
    LOG("Deleting capio file %s from tid=%d", path.c_str(), tid);
    storage_manager->removeFromTid(tid, fd);

    // on_close commits the file once its producers have closed it. A consumer closes the file too,
    // for instance the descriptor that a shell redirection or tar dup's onto a child's stdin, and
    // a producer may close one descriptor while another one still writes, as tar -czf does with
    // the gzip it writes through: neither must commit the file.
    if (!is_prod || CapioCLEngine::get().getCommitRule(path) != capiocl::commitRules::ON_CLOSE ||
        !c_file.closed()) {
        return;
    }
    const auto open_fds = c_file.getFds();
    for (const auto &[open_tid, open_fd] : open_fds) {
        if (is_producer_of(open_tid, path)) {
            LOG("Producer tid=%d still has the file open on fd=%d: not committing", open_tid,
                open_fd);
            return;
        }
    }
    LOG("Capio File %s is closed and commit rule is on_close. setting it to complete",
        path.c_str());
    c_file.setCommitted();
    c_file.dump();
}

void close_handler(const char *str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_close(tid, fd);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP

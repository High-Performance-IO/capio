#ifndef CAPIO_SERVER_HANDLERS_DUP_HPP
#define CAPIO_SERVER_HANDLERS_DUP_HPP

extern StorageManager *storage_manager;

void dup_handler(const char *const str) {
    int tid, old_fd, new_fd;
    sscanf(str, "%d %d %d", &tid, &old_fd, &new_fd);
    START_LOG(gettid(), "call(tid=%d, old_fd=%d, new_fd=%d)", tid, old_fd, new_fd);
    if (old_fd != new_fd) {
        storage_manager->dup(tid, old_fd, new_fd);
    }
}

#endif // CAPIO_SERVER_HANDLERS_DUP_HPP

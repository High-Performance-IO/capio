#ifndef CAPIO_SERVER_HANDLERS_CLONE_HPP
#define CAPIO_SERVER_HANDLERS_CLONE_HPP
#include "storage/manager.hpp"

extern StorageManager *storage_manager;
void clone_handler(const char *const str) {
    pid_t parent_tid, child_tid;
    sscanf(str, "%d %d", &parent_tid, &child_tid);
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);
    storage_manager->clone(parent_tid, child_tid);
    client_manager->unlockClonedChild(child_tid);
}

#endif // CAPIO_SERVER_HANDLERS_CLONE_HPP

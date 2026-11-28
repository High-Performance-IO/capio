#include "handlers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "remote/home_node_service.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/server_state.hpp"

void dup_handler(const char *const str) {
    int tid, old_fd, new_fd;
    sscanf(str, "%d %d %d", &tid, &old_fd, &new_fd);
    START_LOG(gettid(), "call(tid=%d, old_fd=%d, new_fd=%d)", tid, old_fd, new_fd);
    if (old_fd == new_fd) {
        return;
    }
    if (const pid_t pid = client_manager->getPid(tid);
        fs_storage_manager->ownsDescriptor(pid, old_fd)) {
        fs_storage_manager->dup(pid, old_fd, new_fd);
    } else if (!storage_manager->getPath(tid, old_fd).empty()) {
        storage_manager->dup(tid, old_fd, new_fd);
    }
}

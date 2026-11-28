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

void rmdir_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) != 1) {
        return;
    }
    const std::string dir_to_remove = request_text(tid, str, consumed);
    START_LOG(gettid(), "call(path=%s tid=%d)", dir_to_remove.c_str(), tid);
    if (CapioCLEngine::get().isExcluded(dir_to_remove)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    storage_manager->remove(dir_to_remove);
    storage_manager->removeFromDirectory(dir_to_remove);
    int res = home_node_service->remove(dir_to_remove);
    client_manager->replyToClient(tid, res);
}

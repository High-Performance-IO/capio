#include "handlers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "remote/backend.hpp"
#include "remote/home_node_service.hpp"
#include "remote/requests.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/runtime_configuration.hpp"
#include "utils/server_state.hpp"
#include "utils/thread_manager.hpp"

void request_remote_getdents(int tid, int fd, off64_t count, const std::string &home) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, home=%s)", tid, fd, count, home.c_str());

    CapioFile &c_file     = storage_manager->get(tid, fd);
    off64_t offset        = storage_manager->getFileOffset(tid, fd);
    off64_t end_of_read   = offset + count;
    off64_t end_of_sector = c_file.getSectorEnd(offset);

    if (c_file.isCommitted() && (end_of_read <= end_of_sector ||
                                 (end_of_sector == CAPIO_SECTOR_NOT_FOUND ? 0 : end_of_sector) ==
                                     c_file.getRealFileSize())) {
        LOG("Handling local read");
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else if (end_of_read <= end_of_sector) {
        LOG("?");
        c_file.createBuffer(storage_manager->getPath(tid, fd), false);
        client_manager->replyToClient(tid, offset, c_file.getBuffer(), count);
        storage_manager->setFileOffset(tid, fd, offset + count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, true, home);
    }
}

void handle_getdents(int tid, int fd, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::string &app_name                = client_manager->getAppName(tid);
    const std::filesystem::path &path_to_check = storage_manager->getPath(tid, fd);
    bool is_prod           = CapioCLEngine::get().isProducer(path_to_check, app_name);
    auto file_location_opt = home_node_service->getHome(path_to_check);

    if (!file_location_opt && !is_prod) {
        thread_manager().startWaiter(
            thread_name("getdents", path_to_check, tid, "its creation"),
            [tid, fd, count, path_to_check] {
                START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

                if (const auto home = home_node_service->waitForHome(path_to_check);
                    home == backend->get_node_name()) {
                    handle_getdents(tid, fd, count);
                } else {
                    request_remote_getdents(tid, fd, count, home);
                }
            });
    } else if (is_prod || *file_location_opt == backend->get_node_name()) {
        CapioFile &c_file = storage_manager->get(path_to_check);
        off64_t offset    = storage_manager->getFileOffset(tid, fd);
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else {
        LOG("File is remote. Delegating to backend remote read");
        request_remote_getdents(tid, fd, count, *file_location_opt);
    }
}

void getdents_handler(const char *const str) {
    int tid, fd;
    off64_t count, offset = 0;
    sscanf(str, "%d %d %ld %ld", &tid, &fd, &count, &offset);
    const pid_t pid = client_manager->getPid(tid);
    if (fs_storage_manager->ownsDescriptor(pid, fd)) {
        fs_storage_manager->list(tid, pid, fd, offset, count);
        return;
    }
    handle_getdents(tid, fd, count);
}

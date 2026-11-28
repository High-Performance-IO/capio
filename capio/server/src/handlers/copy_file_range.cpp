#include "handlers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "remote/backend.hpp"
#include "remote/handlers/copy_file_range.hpp"
#include "remote/home_node_service.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/server_state.hpp"
#include "utils/types.hpp"
#include "utils/waiter.hpp"

void handle_local_copy_file_range(const CSCopyFileRange_t &copy, const bool is_prod) {
    START_LOG(gettid(), "call(tid=%d, path_in=%s, offset_in=%ld, count=%ld, is_prod=%s)", copy.tid,
              copy.path_in.c_str(), copy.offset_in, copy.count, is_prod ? "true" : "false");

    const CapioFile &c_file_in  = storage_manager->get(copy.path_in);
    const bool file_complete    = c_file_in.isCommitted() || is_prod;
    const off64_t end_of_sector = c_file_in.getSectorEnd(copy.offset_in);
    const bool wait_for_commit  = !file_complete && !CapioCLEngine::get().isFirable(copy.path_in);
    // the file is still being written and the requested range is not all there: this is not the
    // end of the file
    const bool wait_for_data    = !file_complete && (end_of_sector == CAPIO_SECTOR_NOT_FOUND ||
                                                     copy.offset_in + copy.count > end_of_sector);

    if (!wait_for_commit && !wait_for_data) {
        copy_file_range_data(copy);
        return;
    }
    LOG("Waiting on a separate thread for the %s of the input",
        wait_for_commit ? "commit" : "data");
    spawn_waiter([&c_file_in, copy, wait_for_commit] {
        if (wait_for_commit) {
            c_file_in.waitForCommit();
        } else {
            c_file_in.waitForData(copy.offset_in + copy.count);
        }
        copy_file_range_data(copy);
    });
}

void handle_copy_file_range(const CSCopyFileRange_t &copy, const bool is_prod,
                            const std::optional<std::string> &home) {
    if (is_prod || home == backend->get_node_name()) {
        handle_local_copy_file_range(copy, is_prod);
    } else {
        handle_remote_copy_file_range(copy, *home);
    }
}

void copy_file_range_handler(const char *const str) {
    CSCopyFileRange_t copy{};
    sscanf(str, "%d %d %d %ld %ld %ld", &copy.tid, &copy.fd_in, &copy.fd_out, &copy.offset_in,
           &copy.offset_out, &copy.count);
    START_LOG(gettid(),
              "call(tid=%d, fd_in=%d, fd_out=%d, offset_in=%ld, offset_out=%ld, count=%ld)",
              copy.tid, copy.fd_in, copy.fd_out, copy.offset_in, copy.offset_out, copy.count);

    copy.path_in                = storage_manager->getPath(copy.tid, copy.fd_in);
    copy.path_out               = storage_manager->getPath(copy.tid, copy.fd_out);
    const std::string &app_name = client_manager->getAppName(copy.tid);
    const bool is_prod          = CapioCLEngine::get().isProducer(copy.path_in, app_name) ||
                                  client_manager->isProducer(copy.tid, copy.path_in);

    if (!is_prod && !home_node_service->getHome(copy.path_in)) {
        LOG("Waiting on a separate thread for the input to be created");
        spawn_waiter([copy] {
            handle_copy_file_range(copy, false, home_node_service->waitForHome(copy.path_in));
        });
        return;
    }
    handle_copy_file_range(copy, is_prod, home_node_service->getHome(copy.path_in));
}

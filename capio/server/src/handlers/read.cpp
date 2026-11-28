#include "handlers.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <pthread.h>
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
#include "utils/fs_root.hpp"
#include "utils/runtime_configuration.hpp"
#include "utils/server_state.hpp"
#include "utils/waiter.hpp"

namespace {
/// @brief One local read at a time: they move the offsets of descriptors
std::mutex local_read_mutex;
} // namespace

void handle_pending_read(int tid, int fd, long int process_offset, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, process_offset=%ld, count=%ld)", tid, fd,
              process_offset, count);

    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);
    off64_t end_of_sector             = c_file.getSectorEnd(process_offset);
    off64_t end_of_read               = process_offset + count;

    off64_t bytes_read;
    if (end_of_sector > end_of_read) {
        bytes_read = count;
    } else if (end_of_sector == CAPIO_SECTOR_NOT_FOUND) {
        // woken by the commit of a file holding no data at this offset: read 0 bytes
        bytes_read = 0;
    } else {
        bytes_read = end_of_sector - process_offset;
    }

    c_file.createBuffer(path, false);
    // The offset is set before the reply: this runs outside the thread that serves the client
    // requests, so once the client has its answer it may close fd before the offset is stored.
    storage_manager->setFileOffset(tid, fd, process_offset + bytes_read);
    client_manager->replyToClient(tid, process_offset, c_file.getBuffer(), bytes_read);
}

void handle_local_read(int tid, int fd, off64_t count, bool is_prod) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_prod=%s)", tid, fd, count,
              is_prod ? "true" : "false");

    const std::lock_guard<std::mutex> lg(local_read_mutex);
    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);
    off64_t process_offset            = storage_manager->getFileOffset(tid, fd);

    // if a process is the producer of a file, then the file is always complete for that process
    const bool file_complete = c_file.isCommitted() || is_prod;

    if (!(file_complete || CapioCLEngine::get().isFirable(path))) {
        // wait for file to be completed and then do what is done inside handle pending read
        LOG("Data is not available yet. Starting async thread to wait for file availability");
        spawn_waiter([&c_file, tid, fd, count, process_offset] {
            c_file.waitForCommit();
            handle_pending_read(tid, fd, process_offset, count);
        });
        return;
    }

    LOG("Data can be served. Condition met: %s %s", file_complete ? "c_file.is_complete()" : "",
        CapioCLEngine::get().isFirable(path) ? "CapioCLEngine::get().isFirable(path)" : "");

    const off64_t end_of_sector = c_file.getSectorEnd(process_offset);
    const bool sector_not_found = end_of_sector == CAPIO_SECTOR_NOT_FOUND;
    if (sector_not_found && file_complete) {
        LOG("No sector found at offset. returning process_offset without serving data");
        client_manager->replyToClient(tid, process_offset);
        return;
    }

    // The file is still being written and the requested range is not fully available, either
    // because no sector holds data at this offset yet (CAPIO_SECTOR_NOT_FOUND) or because the
    // read goes beyond the end of the sector that holds it. In both cases this is not the end of
    // the file: wait for the data or the commit before serving the read.
    if ((sector_not_found || process_offset + count > end_of_sector) && !file_complete) {
        LOG("Mode is NO_UPDATE, but %s. Awaiting for data on a separate thread before sending "
            "it to client",
            sector_not_found ? "no sector holds data at this offset yet"
                             : "the read goes beyond the end of the available sector");
        spawn_waiter([&c_file, tid, fd, count, process_offset] {
            c_file.waitForData(process_offset + count);
            handle_pending_read(tid, fd, process_offset, count);
        });
        return;
    }

    // Never serve more than the cache line
    const auto read_size = std::min(count, end_of_sector - process_offset);
    LOG("Requested read within end of sector, and data is available. Serving %ld bytes", read_size);

    c_file.createBuffer(path, false);
    client_manager->replyToClient(tid, process_offset, c_file.getBuffer(), read_size);
    storage_manager->setFileOffset(tid, fd, process_offset + read_size);
}

void request_remote_read(int tid, int fd, off64_t count, const std::string &home) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, home=%s)", tid, fd, count, home.c_str());

    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);
    off64_t offset                    = storage_manager->getFileOffset(tid, fd);
    off64_t end_of_read               = offset + count;
    off64_t end_of_sector             = c_file.getSectorEnd(offset);

    if (c_file.isCommitted() && (end_of_read <= end_of_sector ||
                                 (end_of_sector == CAPIO_SECTOR_NOT_FOUND ? 0 : end_of_sector) ==
                                     c_file.getRealFileSize())) {
        LOG("Handling local read");
        handle_local_read(tid, fd, count, true);
    } else if (end_of_read <= end_of_sector) {
        LOG("Data is present locally and can be served to client");
        c_file.createBuffer(path, false);

        client_manager->replyToClient(tid, offset, c_file.getBuffer(), count);
        storage_manager->setFileOffset(tid, fd, offset + count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, false, home);
    }
}

void wait_for_file(const std::filesystem::path &path, int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(path=%s, tid=%d, fd=%d, count=%ld)", path.c_str(), tid, fd, count);

    // check if the file is local or remote
    if (const auto home = home_node_service->waitForHome(path); home == backend->get_node_name()) {
        handle_local_read(tid, fd, count, false);
    } else {
        request_remote_read(tid, fd, count, home);
    }
}

void handle_read(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    const std::string &app_name       = client_manager->getAppName(tid);
    bool is_prod =
        CapioCLEngine::get().isProducer(path, app_name) || client_manager->isProducer(tid, path);
    auto file_location_opt = home_node_service->getHome(path);

    LOG("Is producer= %s", is_prod ? "TRUE" : "FALSE");

    if (!file_location_opt && !is_prod) {
        LOG("Starting thread to wait for file creation");
        // launch a thread that checks when the file is created
        spawn_waiter(wait_for_file, path, tid, fd, count);
    } else if (is_prod || *file_location_opt == backend->get_node_name()) {
        LOG("File is local. handling local read");
        handle_local_read(tid, fd, count, is_prod);
    } else {
        LOG("File is remote. Delegating to backend remote read");
        request_remote_read(tid, fd, count, *file_location_opt);
    }
}

void read_handler(const char *const str) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count);
}

void read_fs_handler(const char *const str) {
    int tid, fd;
    off64_t end_of_read;
    if (sscanf(str, "%d %d %ld", &tid, &fd, &end_of_read) != 3) {
        return;
    }
    const std::filesystem::path path = fs_storage_manager->getPath(client_manager->getPid(tid), fd);

    // Reply -1 when the client will never have to wait for this file, so that it stops asking:
    // the descriptor is unknown to the server (so it is not a CAPIO file), it is a directory, it is
    // excluded, or the caller is the one writing it.
    if (std::error_code error;
        path.empty() || std::filesystem::is_directory(fs_root().real(path), error) ||
        CapioCLEngine::get().isExcluded(path) || FsStorageManager::isProducer(tid, path)) {
        client_manager->replyToClient(tid, -1);
        return;
    }

    if (const auto size = FsStorageManager::readable(path, end_of_read)) {
        client_manager->replyToClient(tid, *size);
        return;
    }
    spawn_waiter([path, tid, end_of_read] {
        client_manager->replyToClient(tid, fs_storage_manager->waitForData(path, end_of_read));
    });
}

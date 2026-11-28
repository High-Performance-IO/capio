#include "handlers.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/home_node_service.hpp"
#include "storage/fs_storage_manager.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/fs_root.hpp"
#include "utils/server_state.hpp"
#include "utils/thread_manager.hpp"

void update_file_metadata(const std::filesystem::path &path, int tid, int fd, bool is_creat,
                          off64_t offset) {
    START_LOG(gettid(), "call(path=%s, client_tid=%d fd=%d, is_creat=%s, offset=%ld)", path.c_str(),
              tid, fd, is_creat ? "true" : "false", offset);

    auto c_file_opt   = storage_manager->tryGet(path);
    CapioFile &c_file = c_file_opt ? c_file_opt->get() : storage_manager->addFile(path);
    storage_manager->addFileToTid(tid, fd, path, offset);

    if (c_file.isFirstWrite() && is_creat) {
        client_manager->registerProducedFile(tid, path);
        c_file.registerFirstWrite();
        home_node_service->setHome(path);
        storage_manager->updateDirectory(tid, path);
    }
}

void wait_for_file_creation(const std::filesystem::path &path, pid_t tid, int fd) {
    START_LOG(gettid(), "wait_for_file_creation(%s, %ld, %d)", path.c_str(), tid, fd);
    home_node_service->waitForHome(path);
    LOG("File %s exists. Allowing execution of posix client", path.c_str());
    update_file_metadata(path, tid, fd, false, 0);
    client_manager->replyToClient(tid, 0);
}

void handle_create(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    // The other servers are asked only when the configuration does not name this client as a
    // producer of the file: lookup() blocks this thread, and with it every client of this
    // server, until all of them have answered.
    const bool is_producer = CapioCLEngine::get().isProducer(path, client_manager->getAppName(tid));
    bool is_creat =
        !(home_node_service->getHome(path) || (!is_producer && home_node_service->lookup(path)));
    update_file_metadata(path, tid, fd, is_creat, 0);
    client_manager->replyToClient(tid, 0);
}

void handle_create_exclusive(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    if (storage_manager->tryGet(path)) {
        client_manager->replyToClient(tid, 1);
    } else {
        client_manager->replyToClient(tid, 0);
        update_file_metadata(path, tid, fd, true, 0);
    }
}

void handle_open(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    // lookup() is last because it may ask the other servers (short circuit evaluation)
    if (home_node_service->getHome(path) || home_node_service->lookup(path)) {
        update_file_metadata(path, tid, fd, false, 0);
        client_manager->replyToClient(tid, 0);
    } else if (CapioCLEngine::get().isProducer(path, client_manager->getAppName(tid)) ||
               client_manager->isProducer(tid, path)) {
        LOG("File does not exist and the client produces it: ENOENT to avoid hangs");
        client_manager->replyToClient(tid, 1);
    } else {
        LOG("File does not exist yet. Halting execution of POSIX client.");
        thread_manager().startWaiter(thread_name("open", path, tid, "its creation"),
                                     wait_for_file_creation, path, tid, fd);
    }
}

void handle_create_fs(const int tid, const std::filesystem::path &path, const bool inherited) {
    const bool has_producer = inherited && client_manager->hasProducer(path);
    if (!has_producer && !CapioCLEngine::get().isExcluded(path)) {
        client_manager->registerProducedFile(tid, path);
        fs_storage_manager->noteNewEntry(path);
    }
}

void handle_open_fs(const int tid, const std::filesystem::path &path) {
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, 0);
        return;
    }
    std::error_code error;
    if (std::filesystem::exists(fs_root().real(path), error) ||
        !FsStorageManager::waitsForCreation(tid, path)) {
        client_manager->replyToClient(tid, 1); // if it is missing, the kernel answers ENOENT
        return;
    }
    thread_manager().startWaiter(thread_name("open", path, tid, "its creation"), [path, tid] {
        fs_storage_manager->waitForCreation(path);
        client_manager->replyToClient(tid, 1);
    });
}

void create_handler(const char *const str) {
    int tid, fd, consumed = 0;
    if (sscanf(str, "%d %d%n", &tid, &fd, &consumed) != 2) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_create(tid, fd, path);
}

void create_exclusive_handler(const char *const str) {
    int tid, fd, consumed = 0;
    if (sscanf(str, "%d %d%n", &tid, &fd, &consumed) != 2) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_create_exclusive(tid, fd, path);
}

void open_handler(const char *const str) {
    int tid, fd, consumed = 0;
    if (sscanf(str, "%d %d%n", &tid, &fd, &consumed) != 2) {
        return;
    }
    const std::string path = request_text(tid, str, consumed);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_open(tid, fd, path);
}

void create_fs_handler(const char *const str) {
    int tid, fd, produced, consumed = 0;
    if (sscanf(str, "%d %d %d%n", &tid, &fd, &produced, &consumed) != 3) {
        return;
    }
    const auto path = fs_request_path(tid, str, consumed);
    if (fd >= 0) {
        fs_storage_manager->addFileToPid(client_manager->getPid(tid), fd, path);
    }
    if (produced != CAPIO_FS_NOT_PRODUCED) {
        handle_create_fs(tid, path, produced == CAPIO_FS_PRODUCED_IF_NO_PRODUCER);
    }
}

void open_fs_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1) {
        handle_open_fs(tid, fs_request_path(tid, str, consumed));
    }
}

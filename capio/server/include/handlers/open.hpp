#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include <chrono>
#include <csignal>
#include <pthread.h>
#include <thread>

#include "common/requests.hpp"
#include "remote/home_node_service.hpp"
#include "utils/common.hpp"
#include "utils/runtime_configuration.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

inline void update_file_metadata(const std::filesystem::path &path, int tid, int fd, bool is_creat,
                                 off64_t offset) {
    START_LOG(gettid(), "call(path=%s, client_tid=%d fd=%d, is_creat=%s, offset=%ld)", path.c_str(),
              tid, fd, is_creat ? "true" : "false", offset);

    // TODO: check the size that the user wrote in the configuration file
    //*caching_info[tid].second += 2;
    auto c_file_opt = storage_manager->tryGet(path);
    CapioFile &c_file =
        (c_file_opt)
            ? c_file_opt->get()
            : storage_manager->add(path, false, RuntimeConfiguration::get().getFileInitialSize());
    storage_manager->addFileToTid(tid, fd, path, offset);

    if (c_file.isFirstWrite() && is_creat) {
        client_manager->registerProducedFile(tid, path);
        c_file.registerFirstWrite();
        home_node_service->setHome(path);
        storage_manager->updateDirectory(tid, path);
    }
}

inline void wait_for_file_creation(const std::filesystem::path &path, pid_t tid, int fd) {
    START_LOG(gettid(), "wait_for_file_creation(%s, %ld, %d)", path.c_str(), tid, fd);
    home_node_service->waitForHome(path);
    LOG("File %s exists. Allowing execution of posix client", path.c_str());
    update_file_metadata(path, tid, fd, false, 0);
    client_manager->replyToClient(tid, 0);
}

inline void handle_create(int tid, int fd, const std::filesystem::path &path) {
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

inline void handle_create_exclusive(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    if (storage_manager->tryGet(path)) {
        client_manager->replyToClient(tid, 1);
    } else {
        client_manager->replyToClient(tid, 0);
        update_file_metadata(path, tid, fd, true, 0);
    }
}

inline void handle_open(int tid, int fd, const std::filesystem::path &path) {
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
        std::thread t(wait_for_file_creation, path, tid, fd);
        t.detach();
    }
}

inline void handle_create_fs(const int tid, const std::filesystem::path &path,
                             const bool inherited) {
    const bool has_producer = inherited && client_manager->hasProducer(path);
    if (!has_producer && !CapioCLEngine::get().isExcluded(path)) {
        client_manager->registerProducedFile(tid, path);
    }
}

inline void handle_open_fs(const int tid, const std::filesystem::path &path) {
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, 0);
        return;
    }
    std::error_code error;
    if (std::filesystem::exists(path, error) || !FsStorageManager::waitsForCreation(tid, path)) {
        client_manager->replyToClient(tid, 1); // if it is missing, the kernel answers ENOENT
        return;
    }
    // The waiter does not get the termination signals: their handler frees memory and exits, and
    // run on a thread that holds a lock (malloc's, CAPIO-CL's) it would wait for it forever.
    // TODO: a thread manager for the threads of the server
    sigset_t signals, previous;
    sigemptyset(&signals);
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        sigaddset(&signals, signal);
    }
    pthread_sigmask(SIG_BLOCK, &signals, &previous);
    std::thread t([path, tid] {
        fs_storage_manager->waitForCreation(path);
        client_manager->replyToClient(tid, 1);
    });
    t.detach();
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

void create_handler(const char *const str) {
    int tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_create(tid, fd, path);
}

void create_exclusive_handler(const char *const str) {
    int tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_create_exclusive(tid, fd, path);
}

void open_handler(const char *const str) {
    int tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    handle_open(tid, fd, path);
}

/// @brief create of the FS variant: "tid path", from a client that created it or opened it to write
void create_fs_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1) {
        handle_create_fs(tid, fs_request_path(tid, str, consumed), false);
    }
}

/// @brief "tid path": @p tid got a writable descriptor of path at exec. It produces the file only
/// if nobody else does, so that "prog > out" does not take it from its producer.
void inherit_fs_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1) {
        handle_create_fs(tid, fs_request_path(tid, str, consumed), true);
    }
}

/// @brief open of the FS variant: "tid path"
void open_fs_handler(const char *const str) {
    int tid, consumed = 0;
    if (sscanf(str, "%d%n", &tid, &consumed) == 1) {
        handle_open_fs(tid, fs_request_path(tid, str, consumed));
    }
}

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

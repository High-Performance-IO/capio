#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include "remote/home_node_service.hpp"
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

    bool is_creat = !(home_node_service->getHome(path) || home_node_service->lookup(path));
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

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

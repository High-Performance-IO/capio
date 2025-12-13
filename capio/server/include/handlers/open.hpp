#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include "utils/location.hpp"

extern ClientManager *client_manager;
extern StorageService *storage_service;

inline void update_file_metadata(const std::filesystem::path &path, int tid, int fd, bool is_creat,
                                 off64_t offset) {
    START_LOG(gettid(), "call(path=%s, client_tid=%d fd=%d, is_creat=%s, offset=%ld)", path.c_str(),
              tid, fd, is_creat ? "true" : "false", offset);

    // TODO: check the size that the user wrote in the configuration file
    //*caching_info[tid].second += 2;
    auto c_file_opt   = storage_service->tryGet(path);
    CapioFile &c_file = (c_file_opt) ? c_file_opt->get()
                                     : storage_service->add(path, false, get_file_initial_size());
    storage_service->addFileToTid(tid, fd, path, offset);

    if (c_file.first_write && is_creat) {
        client_manager->registerProducedFile(tid, path);
        c_file.first_write = false;
        write_file_location(path);
        storage_service->updateDirectory(tid, path);
    }
}

inline void handle_create(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    bool is_creat = !(get_file_location_opt(path) || load_file_location(path));
    update_file_metadata(path, tid, fd, is_creat, 0);
    client_manager->replyToClient(tid, 0);
}

inline void handle_create_exclusive(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    if (storage_service->tryGet(path)) {
        client_manager->replyToClient(tid, 1);
    } else {
        client_manager->replyToClient(tid, 0);
        update_file_metadata(path, tid, fd, true, 0);
    }
}

inline void handle_open(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path.c_str());

    // it is important that check_files_location is the last because is the
    // slowest (short circuit evaluation)
    if (get_file_location_opt(path) || load_file_location(path)) {
        update_file_metadata(path, tid, fd, false, 0);
    } else {
        client_manager->replyToClient(tid, 1);
    }
    client_manager->replyToClient(tid, 0);
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

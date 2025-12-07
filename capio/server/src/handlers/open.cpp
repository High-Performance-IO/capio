#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include "client-manager/client_manager.hpp"
#include "client-manager/handlers.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/env.hpp"
#include "utils/filesystem.hpp"
#include "utils/location.hpp"
#include "utils/metadata.hpp"

extern ClientManager *client_manager;

inline void update_file_metadata(const std::filesystem::path &path, int tid, int fd, bool is_creat,
                                 off64_t offset) {
    START_LOG(gettid(), "call(path=%s, client_tid=%d fd=%d, is_creat=%s, offset=%ld)", path.c_str(),
              tid, fd, is_creat ? "true" : "false", offset);

    // TODO: check the size that the user wrote in the configuration file
    //*caching_info[tid].second += 2;
    auto c_file_opt = get_capio_file_opt(path);
    CapioFile &c_file =
        (c_file_opt) ? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    add_capio_file_to_tid(tid, fd, path, offset);

    if (c_file.first_write && is_creat) {
        client_manager->registerProducedFile(tid, path);
        c_file.first_write = false;
        write_file_location(path);
        update_dir(tid, path);
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
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path);

    bool is_creat = !(get_file_location_opt(path) || load_file_location(path));
    update_file_metadata(path, tid, fd, is_creat, 0);
    client_manager->replyToClient(tid, 0);
}

void create_exclusive_handler(const char *const str) {
    int tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path);

    if (get_capio_file_opt(path)) {
        client_manager->replyToClient(tid, 1);
    } else {
        client_manager->replyToClient(tid, 0);
        update_file_metadata(path, tid, fd, true, 0);
    }
}

void open_handler(const char *const str) {
    int tid, fd;
    char path[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path);
    if (CapioCLEngine::get().isExcluded(path)) {
        client_manager->replyToClient(tid, CAPIO_POSIX_SYSCALL_REQUEST_SKIP);
        return;
    }
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s)", tid, fd, path);

    // it is important that check_files_location is the last because is the
    // slowest (short circuit evaluation)
    if (get_file_location_opt(path) || load_file_location(path)) {
        update_file_metadata(path, tid, fd, false, 0);
    } else {
        client_manager->replyToClient(tid, 1);
    }
    client_manager->replyToClient(tid, 0);
}

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

#ifndef CAPIO_GETDENTS_HPP
#define CAPIO_GETDENTS_HPP

#include <thread>

#include "posix/utils/env.hpp"

#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "utils/location.hpp"

extern StorageManager *storage_manager;

inline void request_remote_getdents(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    CapioFile &c_file     = storage_manager->get(tid, fd);
    off64_t offset        = storage_manager->getFileOffset(tid, fd);
    off64_t end_of_read   = offset + count;
    off64_t end_of_sector = c_file.getSectorEnd(offset);

    if (c_file.complete() && (end_of_read <= end_of_sector ||
                              (end_of_sector == -1 ? 0 : end_of_sector) == c_file.real_file_size)) {
        LOG("Handling local read");
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else if (end_of_read <= end_of_sector) {
        LOG("?");
        c_file.createBufferIfNeeded(storage_manager->getPath(tid, fd), false);
        client_manager->replyToClient(tid, offset, c_file.getBuffer(), count);
        storage_manager->setFileOffset(tid, fd, offset + count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, true);
    }
}

inline void handle_getdents(int tid, int fd, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::string &app_name                = client_manager->getAppName(tid);
    const std::filesystem::path &path_to_check = storage_manager->getPath(tid, fd);
    const std::filesystem::path &capio_dir     = get_capio_dir();
    bool is_prod           = CapioCLEngine::get().isProducer(path_to_check, app_name);
    auto file_location_opt = get_file_location_opt(path_to_check);

    if (!file_location_opt && !is_prod) {
        std::thread t([tid, fd, count, path_to_check] {
            START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

            loop_load_file_location(path_to_check);

            if (strcmp(std::get<0>(get_file_location(path_to_check)), node_name) == 0) {
                handle_getdents(tid, fd, count);
            } else {

                request_remote_getdents(tid, fd, count);
            }
        });
        t.detach();
    } else if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
               capio_dir == path_to_check) {
        CapioFile &c_file = storage_manager->get(path_to_check);
        off64_t offset    = storage_manager->getFileOffset(tid, fd);
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else {
        LOG("File is remote. Delegating to backend remote read");
        request_remote_getdents(tid, fd, count);
    }
}

void getdents_handler(const char *const str) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_getdents(tid, fd, count);
}

#endif // CAPIO_GETDENTS_HPP

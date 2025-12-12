#ifndef CAPIO_GETDENTS_HPP
#define CAPIO_GETDENTS_HPP

#include <thread>

#include "posix/utils/env.hpp"

#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "utils/location.hpp"

extern StorageService *storage_service;

inline void request_remote_getdents(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path = storage_service->getFilePath(tid, fd);
    CapioFile &c_file                 = storage_service->getCapioFile(path)->get();
    off64_t offset                    = storage_service->getFileOffset(tid, fd);
    off64_t end_of_read               = offset + count;
    off64_t end_of_sector             = c_file.get_sector_end(offset);

    if (c_file.is_complete() &&
        (end_of_read <= end_of_sector ||
         (end_of_sector == -1 ? 0 : end_of_sector) == c_file.real_file_size)) {
        LOG("Handling local read");
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else if (end_of_read <= end_of_sector) {
        LOG("?");
        c_file.create_buffer_if_needed(path, false);
        client_manager->replyToClient(tid, offset, c_file.get_buffer(), count);
        storage_service->setFileOffset(tid, fd, offset + count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, true);
    }
}

inline void handle_getdents(int tid, int fd, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::string &app_name            = client_manager->getAppName(tid);
    const std::filesystem::path &path      = storage_service->getFilePath(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    bool is_prod                           = CapioCLEngine::get().isProducer(path, app_name);
    auto file_location_opt                 = get_file_location_opt(path);

    if (!file_location_opt && !is_prod) {
        std::thread t([tid, fd, count] {
            START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

            const std::filesystem::path &path_to_check = storage_service->getFilePath(tid, fd);
            loop_load_file_location(path_to_check);

            if (strcmp(std::get<0>(get_file_location(path_to_check)), node_name) == 0) {
                handle_getdents(tid, fd, count);
            } else {
                const CapioFile &c_file = storage_service->getCapioFile(path_to_check)->get();
                const auto &remote_app  = client_manager->getAppName(tid);
                if (!c_file.is_complete()) {
                    if (const off64_t batch_size =
                            CapioCLEngine::get().getDirectoryFileCount(path_to_check);
                        batch_size > 0) {
                        handle_remote_read_batch_request(tid, fd, count, remote_app,
                                                         path_to_check.parent_path(), batch_size,
                                                         true);
                        return;
                    }
                }
                request_remote_getdents(tid, fd, count);
            }
        });
        t.detach();
    } else if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
               capio_dir == path) {
        CapioFile &c_file = storage_service->getCapioFile(path).value();
        off64_t offset    = storage_service->getFileOffset(tid, fd);
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else {
        LOG("File is remote");
        CapioFile &c_file = storage_service->getCapioFile(path).value();

        if (!c_file.is_complete()) {
            LOG("File not complete");
            const std::string &app_name_inner = client_manager->getAppName(tid);
            LOG("Glob matched");
            std::string prefix = path.parent_path();
            off64_t batch_size = CapioCLEngine::get().getDirectoryFileCount(path);
            if (batch_size > 0) {
                LOG("Handling batch file");
                handle_remote_read_batch_request(tid, fd, count, app_name_inner, prefix, batch_size,
                                                 true);
                return;
            }
        }
        LOG("Delegating to backend remote read");
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

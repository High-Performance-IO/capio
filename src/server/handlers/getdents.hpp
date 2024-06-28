#ifndef CAPIO_GETDENTS_HPP
#define CAPIO_GETDENTS_HPP

#include <thread>

#include "remote/backend.hpp"
#include "remote/requests.hpp"

#include "utils/location.hpp"
#include "utils/metadata.hpp"
#include "utils/producer.hpp"

inline void request_remote_getdents(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t offset                    = get_capio_file_offset(tid, fd);
    off64_t end_of_read               = offset + count;
    off64_t end_of_sector             = c_file.get_sector_end(offset);

    if (c_file.is_complete() &&
        (end_of_read <= end_of_sector ||
         (end_of_sector == -1 ? 0 : end_of_sector) == c_file.real_file_size)) {
        LOG("Handling local read");
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else if (end_of_read <= end_of_sector) {
        LOG("?");
        c_file.create_buffer_if_needed(false);
        send_data_to_client(tid, fd, c_file.get_buffer(), offset, count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, true);
    }
}

inline void handle_getdents(int tid, int fd, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path      = get_capio_file_path(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    bool is_prod                           = is_producer(tid, path);
    auto file_location_opt                 = get_file_location_opt(path);

    if (!file_location_opt && !is_prod) {
        std::thread t([tid, fd, count] {
            START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

            const std::filesystem::path &path_to_check = get_capio_file_path(tid, fd);
            loop_load_file_location(path_to_check);

            if (strcmp(std::get<0>(get_file_location(path_to_check)), node_name) == 0) {
                handle_getdents(tid, fd, count);
            } else {
                const CapioFile &c_file = get_capio_file(path_to_check);
                auto remote_app         = apps.find(tid);
                if (!c_file.is_complete() && remote_app != apps.end()) {
                    long int pos = match_globs(path_to_check);
                    if (pos != -1) {
                        const std::string &remote_app_name = remote_app->second;
                        std::string prefix                 = std::get<0>(metadata_conf_globs[pos]);
                        off64_t batch_size                 = std::get<5>(metadata_conf_globs[pos]);
                        if (batch_size > 0) {
                            handle_remote_read_batch_request(tid, fd, count, remote_app_name,
                                                             prefix, batch_size, true);
                            return;
                        }
                    }
                }
                request_remote_getdents(tid, fd, count);
            }
        });
        t.detach();
    } else if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
               capio_dir == path) {
        CapioFile &c_file = get_capio_file(path);
        off64_t offset    = get_capio_file_offset(tid, fd);
        send_dirent_to_client(tid, fd, c_file, offset, count);
    } else {
        LOG("File is remote");
        CapioFile &c_file = get_capio_file(path);
        auto it           = apps.find(tid);
        if (!c_file.is_complete() && it != apps.end()) {
            LOG("File not complete");
            const std::string &app_name = it->second;
            long int pos                = match_globs(path);
            if (pos != -1) {
                LOG("Glob matched");
                std::string prefix = std::get<0>(metadata_conf_globs[pos]);
                off64_t batch_size = std::get<5>(metadata_conf_globs[pos]);
                if (batch_size > 0) {
                    LOG("Handling batch file");
                    handle_remote_read_batch_request(tid, fd, count, app_name, prefix, batch_size,
                                                     true);
                    return;
                }
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

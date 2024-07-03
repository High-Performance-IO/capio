#ifndef CAPIO_SERVER_HANDLERS_READ_HPP
#define CAPIO_SERVER_HANDLERS_READ_HPP

#include <mutex>
#include <thread>

#include "remote/backend.hpp"
#include "remote/requests.hpp"

#include "capio/metadata.hpp"
#include "utils/location.hpp"
#include "utils/producer.hpp"

std::mutex local_read_mutex;

inline void handle_pending_read(int tid, int fd, long int process_offset, long int count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, process_offset=%ld, count=%ld)", tid, fd,
              process_offset, count);

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t end_of_sector             = c_file.get_sector_end(process_offset);
    off64_t end_of_read               = process_offset + count;

    off64_t bytes_read;
    if (end_of_sector > end_of_read) {
        bytes_read = count;
    } else {
        bytes_read = end_of_sector - process_offset;
    }

    c_file.create_buffer_if_needed(path, false);
    send_data_to_client(tid, fd, c_file.get_buffer(), process_offset, bytes_read);

    // TODO: check if the file was moved to the disk
}

inline void handle_local_read(int tid, int fd, off64_t count, bool is_prod) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_prod=%s)", tid, fd, count,
              is_prod ? "true" : "false");

    const std::lock_guard<std::mutex> lg(local_read_mutex);
    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t process_offset            = get_capio_file_offset(tid, fd);
    int pid                           = pids[tid];
    bool writer                       = writers[pid][path];
    off64_t end_of_sector             = c_file.get_sector_end(process_offset);
    off64_t end_of_read               = process_offset + count;
    std::string_view mode             = c_file.get_mode();
    if (mode == CAPIO_FILE_MODE_UPDATE && !c_file.is_complete() && !writer && !is_prod) {
        // wait for file to be completed and then do what is done inside handle pending read
        LOG("Starting async thread to wait for file availability");
        std::thread t([&c_file, tid, fd, count, process_offset] {
            c_file.wait_for_completion();
            handle_pending_read(tid, fd, process_offset, count);
        });
        t.detach();
    } else if (end_of_read > end_of_sector) {
        if (!is_prod && !writer && !c_file.is_complete()) {
            LOG("Mode is NO_UPDATE. awaiting for data on separate thread before sending it to "
                "client");
            // here if mode is NO_UPDATE, wait for data and then send it
            std::thread t([&c_file, tid, fd, count, process_offset] {
                c_file.wait_for_data(process_offset + count);
                handle_pending_read(tid, fd, process_offset, count);
            });
            t.detach();

        } else {
            LOG("Data is available.");
            if (end_of_sector == -1) {
                LOG("End of sector is -1. returning process_offset without serving data");
                write_response(tid, process_offset);
                return;
            }
            c_file.create_buffer_if_needed(path, false);
            send_data_to_client(tid, fd, c_file.get_buffer(), process_offset,
                                end_of_sector - process_offset);
        }
    } else {
        c_file.create_buffer_if_needed(path, false);
        send_data_to_client(tid, fd, c_file.get_buffer(), process_offset, count);
    }
}

inline void request_remote_read(int tid, int fd, off64_t count) {
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
        handle_local_read(tid, fd, count, true);
    } else if (end_of_read <= end_of_sector) {
        LOG("Data is present locally and can be served to client");
        c_file.create_buffer_if_needed(path, false);
        send_data_to_client(tid, fd, c_file.get_buffer(), offset, count);
    } else {
        LOG("Delegating to backend remote read");
        handle_remote_read_request(tid, fd, count, false);
    }
}

void wait_for_file(const std::filesystem::path &path, int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(path=%s, tid=%d, fd=%d, count=%ld)", path.c_str(), tid, fd, count);

    loop_load_file_location(path);

    // check if the file is local or remote
    if (strcmp(std::get<0>(get_file_location(path)), node_name) == 0) {
        handle_local_read(tid, fd, count, false);
    } else {
        const CapioFile &c_file = get_capio_file(path);
        auto remote_app         = apps.find(tid);
        if (!c_file.is_complete() && remote_app != apps.end()) {
            long int pos = match_globs(path);
            if (pos != -1) {
                const std::string &remote_app_name = remote_app->second;
                std::string prefix                 = std::get<0>(metadata_conf_globs[pos]);
                off64_t batch_size                 = std::get<5>(metadata_conf_globs[pos]);
                if (batch_size > 0) {
                    handle_remote_read_batch_request(tid, fd, count, remote_app_name, prefix,
                                                     batch_size, false);
                    return;
                }
            }
        }
        request_remote_read(tid, fd, count);
    }
}

inline void handle_read(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path      = get_capio_file_path(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    bool is_prod                           = is_producer(tid, path);
    auto file_location_opt                 = get_file_location_opt(path);
    if (!file_location_opt && !is_prod) {
        LOG("Starting thread to wait for file creation");
        // launch a thread that checks when the file is created
        std::thread t(wait_for_file, path, tid, fd, count);
        t.detach();
    } else if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
               capio_dir == path) {
        LOG("File is local. handling local read");
        handle_local_read(tid, fd, count, is_prod);
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
                                                     false);
                    return;
                }
            }
        }
        LOG("Delegating to backend remote read");
        request_remote_read(tid, fd, count);
    }
}

void read_handler(const char *const str) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count);
}

#endif // CAPIO_SERVER_HANDLERS_READ_HPP
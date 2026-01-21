#ifndef CAPIO_SERVER_HANDLERS_READ_HPP
#define CAPIO_SERVER_HANDLERS_READ_HPP

#include <mutex>
#include <thread>

#include "remote/backend.hpp"
#include "remote/requests.hpp"

#include "utils/location.hpp"
#include "utils/metadata.hpp"

std::mutex local_read_mutex;

extern ClientManager *client_manager;

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
    client_manager->replyToClient(tid, process_offset, c_file.get_buffer(), bytes_read);
    set_capio_file_offset(tid, fd, process_offset + bytes_read);

    // TODO: check if the file was moved to the disk
}

inline void handle_local_read(int tid, int fd, off64_t count, bool is_prod) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_prod=%s)", tid, fd, count,
              is_prod ? "true" : "false");

    const std::lock_guard<std::mutex> lg(local_read_mutex);
    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t process_offset            = get_capio_file_offset(tid, fd);

    // if a process is a producer of a file, then the file is always complete for that process
    const bool file_complete = c_file.is_complete() || is_prod;

    // NOTE: do not apply De Morgan laws here as they do not apply due to the composition of
    //       file_complete variable
    if (!CapioCLEngine::get().isFirable(path) && !file_complete) {
        // wait for file to be completed and then do what is done inside handle pending read
        LOG("Data is not yet available. Starting async thread to wait for file availability");
        std::thread t([&c_file, tid, fd, count, process_offset] {
            c_file.wait_for_completion();
            handle_pending_read(tid, fd, process_offset, count);
        });
        t.detach();
        return;
    }

    LOG("Data can be served. Condition met: %% %s", file_complete ? "c_file.is_complete()" : "",
        CapioCLEngine::get().isFirable(path) ? "CapioCLEngine::get().isFirable(path)" : "");

    const off64_t end_of_sector = c_file.get_sector_end(process_offset);
    if (end_of_sector == -1) {
        LOG("End of sector is -1. returning process_offset without serving data");
        client_manager->replyToClient(tid, process_offset);
        return;
    }

    if (process_offset + count > end_of_sector && !file_complete) {
        LOG("Mode is NO_UPDATE, but not enough data is available. Awaiting for data on "
            "separate thread before sending it to client");
        std::thread t([&c_file, tid, fd, count, process_offset] {
            c_file.wait_for_data(process_offset + count);
            handle_pending_read(tid, fd, process_offset, count);
        });
        t.detach();
        return;
    }

    // Ensure it is never served more than the cache line
    const auto read_size = std::min(count, end_of_sector - process_offset);
    LOG("Requested read within end of sector, and data is available. Serving %ld bytes", read_size);

    c_file.create_buffer_if_needed(path, false);
    client_manager->replyToClient(tid, process_offset, c_file.get_buffer(), read_size);
    set_capio_file_offset(tid, fd, process_offset + read_size);
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

        client_manager->replyToClient(tid, offset, c_file.get_buffer(), count);
        set_capio_file_offset(tid, fd, offset + count);
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
        const auto &remote_app  = client_manager->getAppName(tid);
        if (!c_file.is_complete()) {
            std::string prefix = path.parent_path();
            off64_t batch_size = CapioCLEngine::get().getDirectoryFileCount(path);
            if (batch_size > 0) {
                handle_remote_read_batch_request(tid, fd, count, remote_app, prefix, batch_size,
                                                 false);
                return;
            }
        }
        request_remote_read(tid, fd, count);
    }
}

inline void handle_read(int tid, int fd, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);

    const std::filesystem::path &path      = get_capio_file_path(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    const std::string &app_name            = client_manager->getAppName(tid);
    bool is_prod =
        CapioCLEngine::get().isProducer(path, app_name) || client_manager->isProducer(tid, path);
    auto file_location_opt = get_file_location_opt(path);

    LOG("Is producer= %s", is_prod ? "TRUE" : "FALSE");

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
        if (!c_file.is_complete()) {
            LOG("File not complete");
            const std::string &app_name = client_manager->getAppName(tid);

            std::string prefix = path.parent_path();
            off64_t batch_size = CapioCLEngine::get().getDirectoryFileCount(path);
            if (batch_size > 0) {
                LOG("Handling batch file");
                handle_remote_read_batch_request(tid, fd, count, app_name, prefix, batch_size,
                                                 false);
                return;
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
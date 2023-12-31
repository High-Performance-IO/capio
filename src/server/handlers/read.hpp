#ifndef CAPIO_SERVER_HANDLERS_READ_HPP
#define CAPIO_SERVER_HANDLERS_READ_HPP

#include <mutex>
#include <thread>

// TODO: remove this inclusion with some kind of inter process communication
#include "communication_service/backends/mpi.hpp"
#include "utils/location.hpp"
#include "utils/metadata.hpp"
#include "utils/producer.hpp"

CSMyRemotePendingReads_t pending_remote_reads;
std::mutex pending_remote_reads_mutex;
std::mutex local_read_mutex;

inline void handle_pending_read(int tid, int fd, long int process_offset, long int count,
                                bool is_getdents) {
    START_LOG(tid, "call(tid=%d, fd=%d, process_offset=%ld, count=%ld, is_getdents=%s)", tid, fd,
              process_offset, count, is_getdents ? "true" : "false");

    std::string_view path = get_capio_file_path(tid, fd);
    Capio_file &c_file    = init_capio_file(path.data(), false);
    char *p               = c_file.get_buffer();
    off64_t end_of_sector = c_file.get_sector_end(process_offset);
    off64_t end_of_read   = process_offset + count;
    size_t bytes_read;
    if (end_of_sector > end_of_read) {
        end_of_sector = end_of_read;
        bytes_read    = count;
    } else {
        bytes_read = end_of_sector - process_offset;
    }
    if (is_getdents) {
        off64_t dir_size  = c_file.get_stored_size();
        off64_t n_entries = dir_size / CAPIO_THEORETICAL_SIZE_DIRENT64;
        char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
        end_of_sector     = store_dirent(p, p_getdents, dir_size);
        write_response(tid, end_of_sector);
        send_data_to_client(tid, p_getdents + process_offset, end_of_sector - process_offset);
        free(p_getdents);
    } else {
        write_response(tid, end_of_sector);
        send_data_to_client(tid, p + process_offset, bytes_read);
    }
    // TODO: check if the file was moved to the disk
}

inline void handle_local_read(int tid, int fd, off64_t count, bool dir, bool is_getdents,
                              bool is_prod) {
    START_LOG(tid, "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s, is_prod=%s)", tid, fd,
              count, dir ? "true" : "false", is_getdents ? "true" : "false",
              is_prod ? "true" : "false");

    const std::lock_guard<std::mutex> lg(local_read_mutex);
    std::string_view path  = get_capio_file_path(tid, fd);
    Capio_file &c_file     = get_capio_file(path.data());
    off64_t process_offset = get_capio_file_offset(tid, fd);
    int pid                = pids[tid];
    bool writer            = writers[pid][path.data()];
    off64_t end_of_sector  = c_file.get_sector_end(process_offset);
    off64_t end_of_read    = process_offset + count;
    std::string_view mode  = c_file.get_mode();
    if (mode != CAPIO_FILE_MODE_NO_UPDATE && !c_file.complete && !writer && !is_prod && !dir) {
        pending_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
    } else if (end_of_read > end_of_sector) {
        if (!is_prod && !writer && !c_file.complete && !dir) {
            pending_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
        } else {
            if (end_of_sector == -1) {
                write_response(tid, 0);
                return;
            }
            c_file  = init_capio_file(path.data(), false);
            char *p = c_file.get_buffer();
            if (is_getdents || dir) {
                off64_t dir_size  = c_file.get_stored_size();
                off64_t n_entries = dir_size / CAPIO_THEORETICAL_SIZE_DIRENT64;
                char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector     = store_dirent(p, p_getdents, dir_size);
                write_response(tid, end_of_sector);
                send_data_to_client(tid, p_getdents + process_offset,
                                    end_of_sector - process_offset);
                free(p_getdents);
            } else {
                write_response(tid, end_of_sector);
                send_data_to_client(tid, p + process_offset, end_of_sector - process_offset);
            }
        }
    } else {
        c_file  = init_capio_file(path.data(), false);
        char *p = c_file.get_buffer();
        size_t bytes_read;
        bytes_read = count;
        if (is_getdents) {
            off64_t dir_size  = c_file.get_stored_size();
            off64_t n_entries = dir_size / CAPIO_THEORETICAL_SIZE_DIRENT64;
            char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
            end_of_sector     = store_dirent(p, p_getdents, dir_size);
            write_response(tid, end_of_read);
            send_data_to_client(tid, p_getdents + process_offset, bytes_read);
            free(p_getdents);
        } else {
            write_response(tid, end_of_read);
            send_data_to_client(tid, p + process_offset, bytes_read);
        }
    }
}

inline void handle_read(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s, rank=%d)", tid, fd,
              count, dir ? "true" : "false", is_getdents ? "true" : "false", rank);

    std::string_view path                  = get_capio_file_path(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    bool is_prod                           = is_producer(tid, path.data());
    auto file_location_opt                 = get_file_location_opt(path.data());

    if (!file_location_opt && !is_prod) {
        bool found = check_file_location(rank, path.data());
        if (!found) {
            // launch a thread that checks when the file is created
            std::thread t(wait_for_file, tid, fd, count, dir, is_getdents, &pending_remote_reads,
                          &pending_remote_reads_mutex, handle_local_read);
            t.detach();
        }
    }
    if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
        capio_dir == path) {
        handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
    } else {
        Capio_file &c_file = get_capio_file(path.data());
        if (!c_file.complete) {
            auto it  = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                if (!dir) {
                    res = backend->handle_nreads(
                        path.data(), app_name,
                        nodes_helper_rank[std::get<0>(get_file_location(path.data()))]);
                }
            }
            if (res) {
                const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
                pending_remote_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
                return;
            }
        }
        backend->handle_remote_read(tid, fd, count, rank, dir, is_getdents, &pending_remote_reads,
                                    &pending_remote_reads_mutex, handle_local_read);
    }
}

/*
 *	Multithreaded function
 */

void getdents_handler(const char *const str, int rank) {
    START_LOG(gettid(), "call(%s)", str);
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count, true, true, rank);
}

void getdents64_handler(const char *const str, int rank) {
    START_LOG(gettid(), "call(%s)", str);
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count, true, false, rank);
}

void read_handler(const char *const str, int rank) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count, false, false, rank);
}

#endif // CAPIO_SERVER_HANDLERS_READ_HPP

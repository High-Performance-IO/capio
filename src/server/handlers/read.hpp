#ifndef CAPIO_SERVER_HANDLERS_READ_HPP
#define CAPIO_SERVER_HANDLERS_READ_HPP

#include <mutex>
#include <thread>

#include "utils/location.hpp"
#include "utils/metadata.hpp"
#include "utils/util_producer.hpp"

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
        off64_t n_entries = dir_size / THEORETICAL_SIZE_DIRENT64;
        char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
        end_of_sector     = convert_dirent64_to_dirent(p, p_getdents, dir_size);
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
    if (mode != CAPIO_FILE_MODE_NOUPDATE && !c_file.complete && !writer && !is_prod) {
        pending_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
    } else if (end_of_read > end_of_sector) {
        if (!is_prod && !writer && !c_file.complete) {
            pending_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
        } else {
            if (end_of_sector == -1) {
                write_response(tid, 0);
                return;
            }
            c_file  = init_capio_file(path.data(), false);
            char *p = c_file.get_buffer();
            if (is_getdents) {
                off64_t dir_size  = c_file.get_stored_size();
                off64_t n_entries = dir_size / THEORETICAL_SIZE_DIRENT64;
                char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector     = convert_dirent64_to_dirent(p, p_getdents, dir_size);
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
            off64_t n_entries = dir_size / THEORETICAL_SIZE_DIRENT64;
            char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
            end_of_sector     = convert_dirent64_to_dirent(p, p_getdents, dir_size);
            write_response(tid, end_of_read);
            send_data_to_client(tid, p_getdents + process_offset, bytes_read);
            free(p_getdents);
        } else {
            write_response(tid, end_of_read);
            send_data_to_client(tid, p + process_offset, bytes_read);
        }
    }
}

inline bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
                                off64_t end_of_sector, off64_t count, const std::string &path) {
    START_LOG(tid,
              "call(tid=%d, process_ofset=%ld, end_of_read=%ld, "
              "end_of_sector=%ld, count=%ld, path=%s)",
              tid, process_offset, end_of_read, end_of_sector, count, path.c_str());
    bool res = false;
    if (end_of_read <= end_of_sector) {
        Capio_file &c_file = init_capio_file(path.c_str(), false);
        char *p            = c_file.get_buffer();
        write_response(tid, end_of_sector);
        send_data_to_client(tid, p + process_offset, count);
        res = true;
    }
    return res;
}

/*
 * Multithread function
 */

inline void handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir,
                               bool is_getdents) {
    START_LOG(tid, "call(tid=%d, fd=%d, count=%ld, rank=%d, dir=%s, is_getdents=%s)", tid, fd,
              count, rank, dir ? "true" : "false", is_getdents ? "true" : "false");

    // before sending the request to the remote node, it checks
    // in the local cache
    const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
    std::string_view path  = get_capio_file_path(tid, fd);
    Capio_file &c_file     = get_capio_file(path.data());
    size_t real_file_size  = c_file.real_file_size;
    off64_t process_offset = get_capio_file_offset(tid, fd);
    off64_t end_of_read    = process_offset + count;
    off64_t end_of_sector  = c_file.get_sector_end(process_offset);
    std::size_t eos;
    if (end_of_sector == -1) {
        eos = 0;
    } else {
        eos = end_of_sector;
    }
    if (c_file.complete && (end_of_read <= end_of_sector || eos == real_file_size)) {
        handle_local_read(tid, fd, count, dir, is_getdents, true);
        return;
    }
    // when is not complete but mode = append
    if (read_from_local_mem(tid, process_offset, end_of_read, end_of_sector, count, path.data())) {
        // it means end_of_read < end_of_sector
        return;
    }

    // If it is not in cache then send the request to the remote node
    const char *msg;
    std::string str_msg;
    int dest      = nodes_helper_rank[std::get<0>(get_file_location(path.data()))];
    size_t offset = get_capio_file_offset(tid, fd);
    str_msg       = "read " + std::string(path) + " " + std::to_string(rank) + " " +
              std::to_string(offset) + " " + std::to_string(count);
    msg = str_msg.c_str();

    MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    pending_remote_reads[path.data()].emplace_back(tid, fd, count, is_getdents);
}

inline bool handle_nreads(const std::string &path, const std::string &app_name, int dest) {
    START_LOG(gettid(), "call(path=%s, app_name=%s, dest=%d)", path.c_str(), app_name.c_str(),
              dest);

    bool success = false;
    long int pos = match_globs(path);
    if (pos != -1) {
        std::string glob       = std::get<0>(metadata_conf_globs[pos]);
        std::size_t batch_size = std::get<5>(metadata_conf_globs[pos]);
        if (batch_size > 0) {
            char *msg = (char *) malloc(sizeof(char) * (512 + PATH_MAX));
            sprintf(msg, "nrea %zu %s %s %s", batch_size, app_name.c_str(), glob.c_str(),
                    path.c_str());
            MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
            success = true;
            free(msg);
            return success;
        }
    }
    return success;
}

inline void wait_for_file(int tid, int fd, off64_t count, bool dir, bool is_getdents) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s)", tid, fd, count,
              dir ? "true" : "false", is_getdents ? "true" : "false");

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::string_view path_to_check = get_capio_file_path(tid, fd);
    loop_check_files_location(path_to_check.data(), rank);

    // check if the file is local or remote
    if (strcmp(std::get<0>(get_file_location(path_to_check.data())), node_name) == 0) {
        handle_local_read(tid, fd, count, dir, is_getdents, false);
    } else {
        Capio_file &c_file = get_capio_file(path_to_check.data());
        if (!c_file.complete) {
            auto it  = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                res                  = handle_nreads(
                    path_to_check.data(), app_name,
                    nodes_helper_rank[std::get<0>(get_file_location(path_to_check.data()))]);
            }
            if (res) {
                const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
                pending_remote_reads[path_to_check.data()].emplace_back(tid, fd, count,
                                                                        is_getdents);
            }
        }

        handle_remote_read(tid, fd, count, rank, dir, is_getdents);
    }
}

inline void handle_read(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s, rank=%d)", tid, fd,
              count, dir ? "true" : "false", is_getdents ? "true" : "false", rank);

    std::string_view path        = get_capio_file_path(tid, fd);
    const std::string *capio_dir = get_capio_dir();
    bool is_prod                 = is_producer(tid, path.data());

    if (!get_file_location_opt(path.data()) && !is_prod) {
        bool found = check_file_location(rank, path.data());
        if (!found) {
            // launch a thread that checks when the file is created
            std::thread t(wait_for_file, tid, fd, count, dir, is_getdents);
            t.detach();
        }
    }
    if (is_prod || strcmp(std::get<0>(get_file_location(path.data())), node_name) == 0 ||
        *capio_dir == path) {
        handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
    } else {
        Capio_file &c_file = get_capio_file(path.data());
        if (!c_file.complete) {
            auto it  = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                if (!dir) {
                    res = handle_nreads(
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
        handle_remote_read(tid, fd, count, rank, dir, is_getdents);
    }
}

/*
 *	Multithreaded function
 */

inline void solve_remote_reads(size_t bytes_received, size_t offset, size_t file_size,
                               const char *path_c, bool complete) {
    if (pending_remote_reads.find(path_c) == pending_remote_reads.end()) {
        return;
    }

    START_LOG(gettid(),
              "call(bytes_received=%ld, offset=%ld, file_size=%ld, path_c=%s, "
              "complete=%s)",
              bytes_received, offset, file_size, path_c, complete ? "true" : "false");

    Capio_file &c_file    = get_capio_file(path_c);
    c_file.real_file_size = file_size;
    c_file.insert_sector(offset, offset + bytes_received);
    c_file.complete = complete;
    std::string path(path_c);
    const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
    std::list<std::tuple<int, int, long int, bool>> &list_remote_reads = pending_remote_reads[path];
    auto it                                                            = list_remote_reads.begin();
    std::list<std::tuple<int, int, long int, bool>>::iterator prev_it;
    off64_t end_of_sector;
    while (it != list_remote_reads.end()) {
        // TODO: diff between count and bytes_received
        auto &[tid, fd, count, is_getdent] = *it;
        size_t fd_offset                   = get_capio_file_offset(tid, fd);
        if (complete || fd_offset + count <= offset + bytes_received) {
            // this part is equals to the local read (TODO: function)
            end_of_sector = c_file.get_sector_end(fd_offset);
            c_file        = init_capio_file(path_c, false);
            char *p       = c_file.get_buffer();
            size_t bytes_read;
            off64_t end_of_read = fd_offset + count;
            if (end_of_sector > end_of_read) {
                end_of_sector = end_of_read;
                bytes_read    = count;
            } else {
                bytes_read = end_of_sector - fd_offset;
            }
            if (is_getdent) {
                off64_t dir_size  = c_file.get_stored_size();
                off64_t n_entries = dir_size / THEORETICAL_SIZE_DIRENT64;
                char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector     = convert_dirent64_to_dirent(p, p_getdents, dir_size);
                write_response(tid, end_of_sector);
                send_data_to_client(tid, p_getdents + fd_offset, bytes_read);
                free(p_getdents);
            } else {
                write_response(tid, end_of_sector);
                send_data_to_client(tid, p + fd_offset, bytes_read);
            }
            if (it == list_remote_reads.begin()) {
                list_remote_reads.erase(it);
                it = list_remote_reads.begin();
            } else {
                list_remote_reads.erase(it);
                it = std::next(prev_it);
            }
        } else {
            prev_it = it;
            ++it;
        }
    }
}

void getdents_handler(const char *const str, int rank) {
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);
    handle_read(tid, fd, count, true, true, rank);
}

void getdents64_handler(const char *const str, int rank) {
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

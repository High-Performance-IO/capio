#ifndef CAPIO_SERVER_HANDLERS_READ_HPP
#define CAPIO_SERVER_HANDLERS_READ_HPP

#include <mutex>
#include <thread>

#include "remote/backend.hpp"
#include "utils/location.hpp"
#include "utils/metadata.hpp"
#include "utils/producer.hpp"

CSMyRemotePendingReads_t pending_remote_reads;
std::mutex pending_remote_reads_mutex;
std::mutex local_read_mutex;

inline void handle_local_read(int tid, int fd, off64_t count, bool dir, bool is_getdents,
                              bool is_prod) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s, is_prod=%s)", tid,
              fd, count, dir ? "true" : "false", is_getdents ? "true" : "false",
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
    if (mode != CAPIO_FILE_MODE_NO_UPDATE && !c_file.is_complete() && !writer && !is_prod && !dir) {
        pending_reads[path].emplace_back(tid, fd, count, is_getdents);
    } else if (end_of_read > end_of_sector) {
        if (!is_prod && !writer && !c_file.is_complete() && !dir) {
            pending_reads[path].emplace_back(tid, fd, count, is_getdents);
        } else {
            if (end_of_sector == -1) {
                write_response(tid, 0);
                return;
            }
            c_file  = init_capio_file(path, false);
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
        c_file  = init_capio_file(path, false);
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

inline void request_remote_read(int tid, int fd, off64_t count, int rank, bool is_dir,
                                bool is_getdents) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, rank=%d, is_dir=%s, is_getdents=%s)", tid,
              fd, count, rank, is_dir ? "true" : "false", is_getdents ? "true" : "false");

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t offset                    = get_capio_file_offset(tid, fd);
    off64_t end_of_read               = offset + count;
    off64_t end_of_sector             = c_file.get_sector_end(offset);

    if (c_file.is_complete() &&
        (end_of_read <= end_of_sector ||
         (end_of_sector == -1 ? 0 : end_of_sector) == c_file.real_file_size)) {
        LOG("Handling local read");
        handle_local_read(tid, fd, count, is_dir, is_getdents, true);
    } else if (end_of_read <= end_of_sector) {
        c_file  = init_capio_file(path, false);
        char *p = c_file.get_buffer();
        write_response(tid, end_of_sector);
        send_data_to_client(tid, p + offset, count);
    } else {
        backend->handle_remote_read(path, offset, count, rank);
        const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
        pending_remote_reads[path].emplace_back(tid, fd, count, is_getdents);
    }
}

void wait_for_file(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s)", tid, fd, count,
              dir ? "true" : "false", is_getdents ? "true" : "false");

    const std::filesystem::path &path_to_check = get_capio_file_path(tid, fd);
    loop_load_file_location(path_to_check);

    // check if the file is local or remote
    if (strcmp(std::get<0>(get_file_location(path_to_check)), node_name) == 0) {
        handle_local_read(tid, fd, count, dir, is_getdents, false);
    } else {
        CapioFile &c_file = get_capio_file(path_to_check);
        if (!c_file.is_complete()) {
            auto remote_app = apps.find(tid);

            if (remote_app != apps.end()) {
                auto offset          = std::get<0>(get_file_location(path_to_check));
                auto remote_app_name = remote_app->second;
                if (!backend->handle_nreads(path_to_check, remote_app_name,
                                            nodes_helper_rank[offset])) {

                    const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
                    pending_remote_reads[path_to_check].emplace_back(tid, fd, count, is_getdents);
                }
            }
        }
        request_remote_read(tid, fd, count, rank, dir, is_getdents);
    }
}

inline void handle_pending_read(int tid, int fd, long int process_offset, long int count,
                                bool is_getdents) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, process_offset=%ld, count=%ld, is_getdents=%s)", tid,
              fd, process_offset, count, is_getdents ? "true" : "false");

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = init_capio_file(path, false);
    char *p                           = c_file.get_buffer();
    off64_t end_of_sector             = c_file.get_sector_end(process_offset);
    off64_t end_of_read               = process_offset + count;
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

inline void handle_read(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s, rank=%d)", tid, fd,
              count, dir ? "true" : "false", is_getdents ? "true" : "false", rank);

    const std::filesystem::path &path      = get_capio_file_path(tid, fd);
    const std::filesystem::path &capio_dir = get_capio_dir();
    bool is_prod                           = is_producer(tid, path);
    auto file_location_opt                 = get_file_location_opt(path);
    LOG("got to first checkpoint");
    if (!file_location_opt && !is_prod) {
        LOG("got to second checkpoint");
        bool found = load_file_location(path);
        if (!found) {
            LOG("got to third checkpoint");
            // launch a thread that checks when the file is created
            std::thread t(wait_for_file, tid, fd, count, dir, is_getdents, rank);
            t.detach();
        }
        LOG("got to fourth checkpoint");
    }
    if (is_prod || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
        capio_dir == path) {
        LOG("got to 5 checkpoint");
        handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
    } else {
        LOG("got to 6 checkpoint");
        CapioFile &c_file = get_capio_file(path);
        if (!c_file.is_complete()) {
            auto it  = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                if (!dir) {
                    res = backend->handle_nreads(
                        path, app_name, nodes_helper_rank[std::get<0>(get_file_location(path))]);
                }
            }
            if (res) {
                const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
                pending_remote_reads[path].emplace_back(tid, fd, count, is_getdents);
                return;
            }
        }
        request_remote_read(tid, fd, count, rank, dir, is_getdents);
    }
}

inline void handle_read_reply(const std::filesystem::path &path, off64_t size, off64_t offset,
                              off64_t nbytes, bool complete) {
    START_LOG(gettid(), "call(path=%s, size=%ld, offset=%ld, nbytes=%ld, complete=%s)",
              path.c_str(), size, offset, nbytes, complete ? "true" : "false");

    if (pending_remote_reads.find(path) == pending_remote_reads.end()) {
        LOG("No pending remote reads for file %s", path.c_str());
        return;
    }

    CapioFile &c_file     = get_capio_file(path);
    c_file.real_file_size = size;
    c_file.insert_sector(offset, offset + nbytes);
    c_file.set_complete(complete);
    const std::lock_guard<std::mutex> lg(pending_remote_reads_mutex);
    std::list<std::tuple<int, int, long int, bool>> &list_remote_reads = pending_remote_reads[path];
    auto it                                                            = list_remote_reads.begin();
    std::list<std::tuple<int, int, long int, bool>>::iterator prev_it;
    off64_t end_of_sector;
    while (it != list_remote_reads.end()) {
        // TODO: diff between count and nbytes
        auto &[tid, fd, count, is_getdent] = *it;
        off64_t fd_offset                  = get_capio_file_offset(tid, fd);
        if (complete || fd_offset + count <= offset + nbytes) {
            // this part is equals to the local read (TODO: function)
            end_of_sector = c_file.get_sector_end(fd_offset);
            c_file        = init_capio_file(path, false);
            char *p       = c_file.get_buffer();
            off64_t bytes_read;
            off64_t end_of_read = fd_offset + count;
            if (end_of_sector > end_of_read) {
                end_of_sector = end_of_read;
                bytes_read    = count;
            } else {
                bytes_read = end_of_sector - fd_offset;
            }
            if (is_getdent) {
                off64_t dir_size  = c_file.get_stored_size();
                off64_t n_entries = dir_size / CAPIO_THEORETICAL_SIZE_DIRENT64;
                char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector     = store_dirent(p, p_getdents, dir_size);
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

void read_reply_handler(const char *const str, int rank) {
    char path[PATH_MAX];
    off64_t size, offset, nbytes;
    int complete;
    sscanf(str, "%s %ld %ld %ld %d", path, &size, &offset, &nbytes, &complete);
    handle_read_reply(path, size, offset, nbytes, complete);
}

#endif // CAPIO_SERVER_HANDLERS_READ_HPP

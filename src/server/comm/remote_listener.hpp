#ifndef CAPIO_REMOTE_LISTENER_HPP
#define CAPIO_REMOTE_LISTENER_HPP

#include "capio/logger.hpp"
#include "interfaces.hpp"

// backend of capio for communications
backend_interface *backend;

#include "backends/mpi.hpp"

void wait_for_n_files(char *const prefix, std::vector<std::string> *files_path, size_t n_files,
                      int dest, sem_t *n_files_ready) {
    START_LOG(gettid(), "call(prefix=%s, n_files=%ld, dest=%d)", prefix, n_files, dest);

    SEM_WAIT_CHECK(n_files_ready, "n_files_ready");
    backend->send_n_files(prefix, files_path, n_files, dest);

    delete files_path;
    delete n_files_ready;
    delete[] prefix;
}

static inline void wait_for_completion(char *const path, const Capio_file &c_file, int dest,
                                       sem_t *data_is_complete) {
    START_LOG(gettid(), "call(path=%s, dest=%d)", path, dest);

    SEM_WAIT_CHECK(data_is_complete, "data_is_complete");
    backend->serve_remote_stat(path, dest, c_file);

    delete data_is_complete;
    delete[] path;
}

void wait_for_data(char *const path, off64_t offset, int dest, off64_t nbytes,
                   sem_t *data_is_available) {
    START_LOG(gettid(), "call(path=%s, offset=%ld, dest=%d, nbytes=%ld)", path, offset, dest,
              nbytes);

    SEM_WAIT_CHECK(data_is_available, "data_is_available");
    backend->serve_remote_read(path, dest, offset, nbytes, get_capio_file(path).complete);

    delete data_is_available;
    delete[] path;
}

void wait_for_stat(int tid, const std::string &path, int rank,
                   CSMyRemotePendingStats_t *pending_remote_stats,
                   std::mutex *pending_remote_stats_mutex) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    loop_check_files_location(path, rank);
    // check if the file is local or remote
    Capio_file &c_file = get_capio_file(path.c_str());

    if (c_file.complete || c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE ||
        strcmp(std::get<0>(get_file_location(path.c_str())), node_name) == 0) {

        write_response(tid, c_file.get_file_size());
        write_response(tid, static_cast<int>(c_file.is_dir() ? 1 : 0));

    } else {
        backend->handle_remote_stat(tid, path, rank, pending_remote_stats,
                                    pending_remote_stats_mutex);
    }
}

void wait_for_file(int tid, int fd, off64_t count, bool dir, bool is_getdents, int rank,
                   CSMyRemotePendingReads_t *pending_remote_reads,
                   std::mutex *pending_remote_reads_mutex,
                   void (*handle_local_read)(int, int, off64_t, bool, bool, bool)) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, dir=%s, is_getdents=%s)", tid, fd, count,
              dir ? "true" : "false", is_getdents ? "true" : "false");

    auto path_to_check = get_capio_file_path(tid, fd).data();
    loop_check_files_location(path_to_check, rank);

    // check if the file is local or remote
    if (strcmp(std::get<0>(get_file_location(path_to_check)), node_name) == 0) {
        handle_local_read(tid, fd, count, dir, is_getdents, false);
    } else {
        Capio_file &c_file = get_capio_file(path_to_check);
        if (!c_file.complete) {
            auto remote_app = apps.find(tid);

            if (remote_app != apps.end()) {
                auto offset          = std::get<0>(get_file_location(path_to_check));
                auto remote_app_name = remote_app->second;
                if (!backend->handle_nreads(path_to_check, remote_app_name,
                                            nodes_helper_rank[offset])) {

                    const std::lock_guard<std::mutex> lg(*pending_remote_reads_mutex);
                    (*pending_remote_reads)[path_to_check].emplace_back(tid, fd, count,
                                                                        is_getdents);
                }
            }
        }

        backend->handle_remote_read(tid, fd, count, rank, dir, is_getdents, pending_remote_reads,
                                    pending_remote_reads_mutex, handle_local_read);
    }
}

void solve_remote_reads(size_t bytes_received, size_t offset, size_t file_size, const char *path_c,
                        bool complete, CSMyRemotePendingReads_t *pending_remote_reads,
                        std::mutex *pending_remote_reads_mutex) {
    if (pending_remote_reads->find(path_c) == pending_remote_reads->end()) {
        return;
    }

    START_LOG(gettid(),
              "call(bytes_received=%ld, offset=%ld, file_size=%ld, path_c=%s, complete=%s)",
              bytes_received, offset, file_size, path_c, complete ? "true" : "false");

    Capio_file &c_file    = get_capio_file(path_c);
    c_file.real_file_size = file_size;
    c_file.insert_sector(offset, offset + bytes_received);
    c_file.complete = complete;
    std::string path(path_c);
    const std::lock_guard<std::mutex> lg(*pending_remote_reads_mutex);
    std::list<std::tuple<int, int, long int, bool>> &list_remote_reads =
        (*pending_remote_reads)[path];
    auto it = list_remote_reads.begin();
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
                off64_t n_entries = dir_size / CAPIO_THEORETICAL_SIZE_DIRENT64;
                char *p_getdents  = (char *) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector     = store_dirent(p, p_getdents, dir_size);
                write_response(tid, end_of_sector);
                send_data_to_client(tid, p_getdents + fd_offset, bytes_read);
                delete[] p_getdents;
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

bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
                         off64_t end_of_sector, off64_t count, const std::string &path) {
    START_LOG(tid,
              "call(tid=%d, process_ofset=%ld, end_of_read=%ld, "
              "end_of_sector=%ld, count=%ld, path=%s)",
              tid, process_offset, end_of_read, end_of_sector, count, path.c_str());

    if (end_of_read <= end_of_sector) {
        Capio_file &c_file = init_capio_file(path.c_str(), false);
        char *p            = c_file.get_buffer();
        write_response(tid, end_of_sector);
        send_data_to_client(tid, p + process_offset, count);
        return true;
    }
    return false;
}

std::vector<std::string> *files_available(const std::string &prefix, const std::string &app,
                                          const std::string &path_file, int n_files) {
    START_LOG(gettid(), "call(prefix=%s, app=%s, path_file=%s, n_files=%d)", prefix.c_str(),
              app.c_str(), path_file.c_str(), n_files);

    auto files_to_send                     = new std::vector<std::string>;
    std::unordered_set<std::string> &files = files_sent[app];

    auto capio_file_opt = get_capio_file_opt(path_file.c_str());

    if (capio_file_opt) {
        Capio_file &c_file = capio_file_opt->get();
        if (c_file.complete) {
            files_to_send->emplace_back(path_file);
            files.insert(path_file);
        }
    } else {
        return files_to_send;
    }

    for (auto path : get_capio_file_paths()) { // DATA RACE on files_metadata
        auto file_location_opt = get_file_location_opt(path.data());

        if (files.find(path.data()) == files.end() && file_location_opt &&
            strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 &&
            path.compare(0, prefix.length(), prefix) == 0) {

            Capio_file &c_file = get_capio_file(path.data());
            if (c_file.complete && !c_file.is_dir()) {
                files_to_send->emplace_back(path.data());
                files.insert(path.data());
            }
        }
    }
    return files_to_send;
}

void recv_nfiles(RemoteRequest *request, void *arg1, void *arg2) {

    auto pending_remote_reads       = static_cast<CSMyRemotePendingReads_t *>(arg1);
    auto pending_remote_reads_mutex = static_cast<std::mutex *>(arg2);

    auto buf_recv = request->getRequest();
    auto source   = request->getSource();
    START_LOG(gettid(), "call(%s, %d)", buf_recv, source);

    std::string path, bytes_received, prefix;
    std::vector<std::pair<std::string, std::string>> files;
    std::stringstream input_stringstream(buf_recv);
    getline(input_stringstream, path, ' ');
    getline(input_stringstream, prefix, ' ');

    while (getline(input_stringstream, path, ' ')) { // TODO: split sems
        path = prefix + path;
        getline(input_stringstream, bytes_received, ' ');
        files.emplace_back(path, bytes_received);
        long int file_size = std::stoi(bytes_received);
        void *p_shm;
        auto c_file_opt = get_capio_file_opt(path.c_str());
        if (c_file_opt) {
            Capio_file &c_file   = init_capio_file(path.c_str(), false);
            p_shm                = c_file.get_buffer();
            size_t file_shm_size = c_file.get_buf_size();
            if (file_size > file_shm_size) {
                p_shm = expand_memory_for_file(path, file_size, c_file);
            }
            c_file.first_write = false;
        } else {
            auto node_name_src = rank_to_node[source];
            add_file_location(path, node_name_src.c_str(), -1);
            p_shm              = new char[file_size];
            Capio_file &c_file = create_capio_file(path, false, file_size);
            c_file.insert_sector(0, file_size);
            c_file.complete       = true;
            c_file.real_file_size = file_size;
            c_file.first_write    = false;
        }
        backend->recv_file((char *) p_shm, source, file_size);
    }

    for (const auto &pair : files) {
        auto file_path        = pair.first;
        auto bytes_received_1 = pair.second;
        solve_remote_reads(std::stol(bytes_received_1), 0, std::stol(bytes_received_1),
                           file_path.c_str(), true, pending_remote_reads,
                           pending_remote_reads_mutex);
    }
}

void remote_listener_handle_stat_reply(RemoteRequest *request, void *arg1, void *arg2) {
    START_LOG(gettid(), "call(buf_recv=%s)", request->getRequest());

    char *path_c = new char[1024];
    off64_t size;
    int dir, trash;
    sscanf(request->getRequest(), "%d %s %ld %d", &trash, path_c, &size, &dir);
    stat_reply_request(path_c, size, dir);
    delete[] path_c;
}

void remote_listener_stat_req(RemoteRequest *request, void *arg1, void *arg2) {
    const char *buf_recv = request->getRequest();
    START_LOG(gettid(), "call(%s)", buf_recv);
    auto path_c = new char[PATH_MAX];
    int dest;

    sscanf(buf_recv, "%d %s", &dest, path_c);

    Capio_file &c_file = get_capio_file(path_c);
    if (c_file.complete) {
        LOG("file is complete. serving file");
        backend->serve_remote_stat(path_c, dest, c_file);
    } else { // wait for completion
        LOG("File is not complete. awaiting completion on different thread");
        auto sem = new sem_t;

        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init in remote_listener_stat_req");
        }
        clients_remote_pending_stat[path_c].emplace_back(sem);
        std::thread t(wait_for_completion, path_c, std::cref(c_file), dest, sem);
        t.detach();
    }
}

void remote_listener_nreads_req(RemoteRequest *request, void *arg1, void *arg2) {
    START_LOG(gettid(), "call(%ld, %d)", request->getRequest(), request->getSource());
    auto dest      = request->getSource();
    auto prefix    = new char[PATH_MAX];
    auto path_file = new char[PATH_MAX];
    auto app_name  = new char[512];
    std::size_t n_files;
    auto sem = new sem_t;
    int trash;
    sscanf(request->getRequest(), "%d %zu %s %s %s", &trash, &n_files, app_name, prefix, path_file);
    n_files = find_batch_size(prefix, metadata_conf_globs);

    SEM_WAIT_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
    std::vector<std::string> *files = files_available(prefix, app_name, path_file, n_files);
    SEM_POST_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");

    if (files->size() == n_files) {
        backend->send_n_files(prefix, files, n_files, dest);
        delete files;
    } else {
        /*
         * create a thread that waits for the completion of such
         * files and then send those files
         */
        auto prefix_c = new char[strlen(prefix)];

        strcpy(prefix_c, prefix);

        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init in remote_listener_nreads_req");
        }
        std::thread t(wait_for_n_files, prefix, files, n_files, dest, sem);

        SEM_WAIT_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
        clients_remote_pending_nfiles[app_name].emplace_back(prefix_c, n_files, dest, files, sem);
        SEM_POST_CHECK(&clients_remote_pending_nfiles_sem, "clients_remote_pending_nfiles_sem");
    }

    delete[] prefix;
    delete[] path_file;
    delete[] app_name;
}

void remote_listener_remote_read(RemoteRequest *request, void *arg1, void *arg2) {
    // schema msg received: "read path dest offset nbytes"
    START_LOG(gettid(), "call()");
    auto path_c = new char[PATH_MAX];

    int dest, trash;
    long int offset, nbytes;
    sscanf(request->getRequest(), "%d %s %d %ld %ld", &trash, path_c, &dest, &offset, &nbytes);

    // check if the data is available
    Capio_file &c_file = get_capio_file(path_c);

    bool data_available = (offset + nbytes <= c_file.get_stored_size());
    if (c_file.complete || (c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE && data_available)) {
        backend->serve_remote_read(path_c, dest, offset, nbytes, c_file.complete);
    } else {
        auto sem = new sem_t;

        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init sem");
        }
        clients_remote_pending_reads[path_c].emplace_back(offset, nbytes, sem);
        std::thread t(wait_for_data, path_c, offset, dest, nbytes, sem);
        t.detach();
    }
}

void remote_listener_remote_sending(RemoteRequest *request, void *arg1, void *arg2) {
    off64_t bytes_received, offset;
    std::string path;
    path.reserve(1024);
    int complete_tmp, trash;
    size_t file_size;
    sscanf(request->getRequest(), "%d %s %ld %ld %d %zu", &trash, path.data(), &offset,
           &bytes_received, &complete_tmp, &file_size);
    bool complete      = complete_tmp;
    void *file_shm     = nullptr;
    Capio_file &c_file = init_capio_file(path.c_str(), file_shm);
    if (bytes_received != 0) {
        auto file_shm_size  = c_file.get_buf_size();
        auto file_size_recv = offset + bytes_received;
        if (file_size_recv > file_shm_size) {
            file_shm = expand_memory_for_file(path, file_size_recv, c_file);
        }
        backend->recv_file((char *) file_shm + offset, request->getSource(), bytes_received);
        bytes_received *= sizeof(char);
    }

    solve_remote_reads(bytes_received, offset, file_size, path.c_str(), complete,
                       &pending_remote_reads, &pending_remote_reads_mutex);
}

void capio_remote_listener() {
    START_LOG(gettid(), "call()");

    std::array<CComsHandler_t, CAPIO_SERVER_NR_REQUEST> remote_request_map{};
    remote_request_map[CAPIO_SERVER_REQUEST_READ]    = remote_listener_remote_read;
    remote_request_map[CAPIO_SERVER_REQUEST_SENDING] = remote_listener_remote_sending;
    remote_request_map[CAPIO_SERVER_REQUEST_STAT]    = remote_listener_stat_req;
    remote_request_map[CAPIO_SERVER_REQUEST_SIZE]    = remote_listener_handle_stat_reply;
    remote_request_map[CAPIO_SERVER_REQUEST_N_READ]  = remote_listener_nreads_req;
    remote_request_map[CAPIO_SERVER_REQUEST_N_SEND]  = recv_nfiles;

    sem_wait(&internal_server_sem);
    while (true) {
        auto request = backend->read_next_request();

        remote_request_map[request->getRequestCode()](request, (void *) &pending_remote_stats,
                                                      (void *) &pending_remote_stats_mutex);
    }
}

#endif // CAPIO_REMOTE_LISTENER_HPP

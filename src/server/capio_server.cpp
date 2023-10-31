#include <dirent.h>
#include <fcntl.h>
#include <mpi.h>
#include <semaphore.h>
#include <singleheader/simdjson.h>
#include <unistd.h>

#include <algorithm>
#include <args.hxx>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "capio/env.hpp"
#include "capio/logger.hpp"
#include "utils/capio_file.hpp"
#include "utils/common.hpp"
#include "utils/env.hpp"
#include "utils/json.hpp"
#include "utils/metadata.hpp"
#include "utils/requests.hpp" //TODO: check whether to include requests here or in capio posix

using namespace simdjson;

MPI_Request req;
int n_servers;

/*
 * For multithreading:
 * tid -> pid*/
CSPidsMap_T pids;

// tid -> application name
CSAppsMap_t apps;

// application name -> set of files already sent
CSFilesSentMap_t files_sent;

// tid -> (client_to_server_data_buf, server_to_client_data_buf)
CSDataBufferMap_t data_buffers;

/*
 * pid -> pathname -> bool
 * Different threads with the same pid are treated as a single writer
 */
CSWritersMap_t writers;

// node -> rank
CSNodesHelperRankMap_t nodes_helper_rank;

// rank -> node
CSRankToNodeMap_t rank_to_node;

/*
 * It contains all the reads requested by local processes to read files that
 * are in the local node for which the data is not yet available. path -> [(tid,
 * fd, numbytes, is_getdents), ...]
 */
CSPendingReadsMap_t pending_reads;

/*
 * It contains all the read requested by other nodes for which the data is not
 * yet available path -> [(offset, numbytes, sem_pointer), ...]
 */

CSClientsRemotePendingReads_t clients_remote_pending_reads;

CSClientsRemotePendingStats_t clients_remote_pending_stat;

// it contains the file saved on disk
CSOnDiskMap_t on_disk;

CSClientsRemotePendingNFilesMap_t clients_remote_pending_nfiles;

// name of the node
char node_name[MPI_MAX_PROCESSOR_NAME];

sem_t internal_server_sem;
sem_t remote_read_sem;
sem_t clients_remote_pending_nfiles_sem;

#include "handlers.hpp"
#include "utils/location.hpp"
#include "utils/signals.hpp"

static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table() {
    std::array<CSHandler_t, CAPIO_NR_REQUESTS> _request_handlers{0};

    _request_handlers[CAPIO_REQUEST_ACCESS]              = access_handler;
    _request_handlers[CAPIO_REQUEST_CLONE]               = clone_handler;
    _request_handlers[CAPIO_REQUEST_CLOSE]               = close_handler;
    _request_handlers[CAPIO_REQUEST_CREATE]              = create_handler;
    _request_handlers[CAPIO_REQUEST_CREATE_EXCLUSIVE]    = create_exclusive_handler;
    _request_handlers[CAPIO_REQUEST_DUP]                 = dup_handler;
    _request_handlers[CAPIO_REQUEST_EXIT_GROUP]          = exit_group_handler;
    _request_handlers[CAPIO_REQUEST_FSTAT]               = fstat_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS]            = getdents_handler;
    _request_handlers[CAPIO_REQUEST_GETDENTS64]          = getdents64_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_NAMED]     = handshake_named_handler;
    _request_handlers[CAPIO_REQUEST_HANDSHAKE_ANONYMOUS] = handshake_anonymous_handler;
    _request_handlers[CAPIO_REQUEST_MKDIR]               = mkdir_handler;
    _request_handlers[CAPIO_REQUEST_OPEN]                = open_handler;
    _request_handlers[CAPIO_REQUEST_READ]                = read_handler;
    _request_handlers[CAPIO_REQUEST_RENAME]              = rename_handler;
    _request_handlers[CAPIO_REQUEST_RMDIR]               = rmdir_handler;
    _request_handlers[CAPIO_REQUEST_SEEK]                = lseek_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_DATA]           = seek_data_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_END]            = seek_end_handler;
    _request_handlers[CAPIO_REQUEST_SEEK_HOLE]           = seek_hole_handler;
    _request_handlers[CAPIO_REQUEST_STAT]                = stat_handler;
    _request_handlers[CAPIO_REQUEST_STAT_REPLY]          = stat_reply_handler;
    _request_handlers[CAPIO_REQUEST_UNLINK]              = unlink_handler;
    _request_handlers[CAPIO_REQUEST_WRITE]               = write_handler;

    return _request_handlers;
}

void handshake_servers(int rank) {
    START_LOG(gettid(), "call(%d)", rank);

    char *buf;
    buf = (char *) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
    if (buf == nullptr) {
        ERR_EXIT("malloc handshake_servers");
    }
    if (rank == 0) {
        clean_files_location(n_servers);
    }
    for (int i = 0; i < n_servers; i += 1) {
        if (i != rank) {
            MPI_Send(node_name, strlen(node_name), MPI_CHAR, i, 0,
                     MPI_COMM_WORLD); // TODO: possible deadlock
            std::fill(buf, buf + MPI_MAX_PROCESSOR_NAME, 0);
            MPI_Recv(buf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, i, 0, MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);
            nodes_helper_rank[buf] = i;
            rank_to_node[i]        = buf;
        }
    }
    free(buf);
}

void capio_server(int rank) {
    static const std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers =
        build_request_handlers_table();

    START_LOG(gettid(), "call(rank=%d)", rank);

    MPI_Comm_size(MPI_COMM_WORLD, &n_servers);
    setup_signal_handlers();
    handshake_servers(rank);
    open_files_location(rank);
    int pid                      = getpid();
    const std::string *capio_dir = get_capio_dir();
    create_dir(pid, capio_dir->c_str(), rank,
               true); // TODO: can be a problem if a process execute readdir
    // on capio_dir

    init_server();

    if (sem_post(&internal_server_sem) == -1) {
        ERR_EXIT("sem_post internal_server_sem in capio_server");
    }

    auto str = std::unique_ptr<char[]>(new char[CAPIO_REQUEST_MAX_SIZE]);
    while (true) {
        int code = read_next_request(str.get());
        if (code < 0 || code > CAPIO_NR_REQUESTS) {
            ERR_EXIT("Received an invalid request code %d", code);
        }
        request_handlers[code](str.get(), rank);
    }
}

void send_file(char *shm, long int nbytes, int dest) {
    START_LOG(gettid(), "call(%s), %ld, %d", shm, nbytes, dest);
    if (nbytes == 0) {
        return;
    }
    long int num_elements_to_send = 0;
    // MPI_Request req;
    for (long int k = 0; k < nbytes; k += num_elements_to_send) {
        if (nbytes - k > 1024L * 1024 * 1024) {
            num_elements_to_send = 1024L * 1024 * 1024;
        } else {
            num_elements_to_send = nbytes - k;
        }
        MPI_Isend(shm + k, num_elements_to_send, MPI_BYTE, dest, 0, MPI_COMM_WORLD, &req);
    }
}

// TODO: refactor offset_str and offset
inline void serve_remote_read(const char *path_c, int dest, long int offset, long int nbytes,
                              int complete) {
    START_LOG(gettid(), "call(pach_c=%s, dest=%d, offset=%ld, nbytes=%ld, complete=%d)", path_c,
              dest, offset, nbytes, complete);
    if (sem_wait(&remote_read_sem) == -1) {
        ERR_EXIT("sem_wait remote_read_sem in serve_remote_read");
    }

    char *buf_send;
    // Send all the rest of the file not only the number of bytes requested
    // Useful for caching
    Capio_file &c_file = get_capio_file(path_c);
    size_t file_size   = c_file.get_stored_size();
    nbytes             = file_size - offset;

    off64_t prefetch_data_size = get_prefetch_data_size();

    if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
        nbytes = prefetch_data_size;
    }

    std::string nbytes_str     = std::to_string(nbytes);
    const char *nbytes_cstr    = nbytes_str.c_str();
    std::string offset_str     = std::to_string(offset);
    const char *offset_cstr    = offset_str.c_str();
    std::string complete_str   = std::to_string(complete);
    const char *complete_cstr  = complete_str.c_str();
    std::string file_size_str  = std::to_string(file_size);
    const char *file_size_cstr = file_size_str.c_str();
    const char *s1             = "sending";
    const size_t len1          = strlen(s1);
    const size_t len2          = strlen(path_c);
    const size_t len3          = strlen(offset_cstr);
    const size_t len4          = strlen(nbytes_cstr);
    const size_t len5          = strlen(complete_cstr);
    const size_t len6          = strlen(file_size_cstr);
    buf_send                   = (char *) malloc((len1 + len2 + len3 + len4 + len5 + len6 + 6) *
                                                 sizeof(char)); // TODO:add malloc check
    sprintf(buf_send, "%s %s %s %s %s %s", s1, path_c, offset_cstr, nbytes_cstr, complete_cstr,
            file_size_cstr);

    // send warning
    MPI_Send(buf_send, strlen(buf_send) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    free(buf_send);
    // send data
    send_file(c_file.get_buffer() + offset, nbytes, dest);

    if (sem_post(&remote_read_sem) == -1) {
        ERR_EXIT("sem_post remote_read_sem in serve_remote_read");
    }
}

void wait_for_data(char *const path, off64_t offset, int dest, off64_t nbytes, sem_t *sem) {
    START_LOG(gettid(), "call(path=%s, offset=%ld, dest=%d, nbytes=%ld)", path, offset, dest,
              nbytes);
    if (sem_wait(sem) == -1) {
        ERR_EXIT("sem_wait in wait_for_data");
    }

    Capio_file &c_file = get_capio_file(path);
    bool complete      = c_file.complete;
    serve_remote_read(path, dest, offset, nbytes, complete);
    free(path);
    free(sem);
}

int find_batch_size(const std::string &glob) {
    START_LOG(gettid(), "call(%s)", glob.c_str());
    bool found = false;
    int n_files;
    std::size_t i = 0;

    while (!found && i < metadata_conf_globs.size()) {
        found = glob == std::get<0>(metadata_conf_globs[i]);
        ++i;
    }

    if (found) {
        n_files = std::get<5>(metadata_conf_globs[i - 1]);
    } else {
        n_files = -1;
    }

    return n_files;
}

void send_n_files(const std::string &prefix, std::vector<std::string> *files_to_send, int n_files,
                  int dest) {
    START_LOG(gettid(), "call(prefix=%s, files_to_send=%ld, n_files=%d, dest=%d)", prefix.c_str(),
              files_to_send, n_files, dest);

    std::string msg      = "nsend " + prefix;
    size_t prefix_length = prefix.length();
    for (const std::string &path : *files_to_send) {
        Capio_file &c_file = get_capio_file(path.c_str());
        msg += " " + path.substr(prefix_length);
        size_t file_size = c_file.get_stored_size();
        msg += " " + std::to_string(file_size);
    }
    const char *msg_cstr = msg.c_str();

    MPI_Send(msg_cstr, strlen(msg_cstr) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);

    for (const std::string &path : *files_to_send) {
        Capio_file c_file = get_capio_file(path.c_str());
        send_file(c_file.get_buffer(), c_file.get_stored_size(), dest);
    }
}

void wait_for_n_files(char *const prefix, std::vector<std::string> *files_path, size_t n_files,
                      int dest, sem_t *sem) {
    START_LOG(gettid(), "call(prefix=%s, n_files=%ld, dest=%d)", prefix, n_files, dest);

    if (sem_wait(sem) == -1) {
        ERR_EXIT("sem_wait in wait_for_n_files");
    }
    send_n_files(prefix, files_path, n_files, dest);
    delete files_path;
    free(prefix);
    free(sem);
}

std::vector<std::string> *files_available(const std::string &prefix, const std::string &app,
                                          const std::string &path_file, int n_files) {
    START_LOG(gettid(), "call(prefix=%s, app=%s, path_file=%s, n_files=%d)", prefix.c_str(),
              app.c_str(), path_file.c_str(), n_files);

    int n_files_completed                  = 0;
    size_t prefix_length                   = prefix.length();
    auto *files_to_send                    = new std::vector<std::string>;
    std::unordered_set<std::string> &files = files_sent[app];

    auto capio_file_opt = get_capio_file_opt(path_file.c_str());
    if (capio_file_opt) {
        Capio_file &c_file = capio_file_opt->get();
        if (c_file.complete) {
            files_to_send->emplace_back(path_file);
            ++n_files_completed;
            files.insert(path_file);
        }
    } else {
        return files_to_send;
    }
    for (auto path : get_capio_file_paths()) { // DATA RACE on files_metadata
        auto file_location_opt = get_file_location_opt(path.data());
        if (files.find(path.data()) == files.end() && file_location_opt &&
            strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 &&
            path.compare(0, prefix_length, prefix) == 0) {
            Capio_file &c_file = get_capio_file(path.data());
            if (c_file.complete && !c_file.is_dir()) {
                files_to_send->emplace_back(path.data());
                ++n_files_completed;
                files.insert(path.data());
            }
        }
    }
    return files_to_send;
}

void helper_nreads_req(char *buf_recv, int dest) {
    START_LOG(gettid(), "call(%ld, %d)", buf_recv, dest);
    char *prefix    = (char *) malloc(sizeof(char) * PATH_MAX);
    char *path_file = (char *) malloc(sizeof(char) * PATH_MAX);
    char *app_name  = (char *) malloc(sizeof(char) * 512);
    std::size_t n_files;
    sscanf(buf_recv, "nrea %zu %s %s %s", &n_files, app_name, prefix, path_file);
    n_files = find_batch_size(prefix);
    if (sem_wait(&clients_remote_pending_nfiles_sem) ==
        -1) { // important even if not using the data structure
        ERR_EXIT("sem_wait clients_remote_pending_nfiles_sem in helper_nreads_req");
    }
    std::vector<std::string> *files = files_available(prefix, app_name, path_file, n_files);
    if (sem_post(&clients_remote_pending_nfiles_sem) == -1) {
        ERR_EXIT("sem_post clients_remote_pending_nfiles_sem in helper_nreads_req");
    }
    if (files->size() == n_files) {
        send_n_files(prefix, files, n_files, dest);
        delete files;
    } else {
        /*
         * create a thread that waits for the completion of such
         * files and then send those files
         */
        char *prefix_c = (char *) malloc(sizeof(char) * strlen(prefix));
        if (prefix_c == nullptr) {
            ERR_EXIT("malloc 2 in capio_helper");
        }
        strcpy(prefix_c, prefix);
        sem_t *sem = (sem_t *) malloc(sizeof(sem_t));
        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init in helper_nreads_req");
        }
        std::thread t(wait_for_n_files, prefix, files, n_files, dest, sem);
        if (sem_wait(&clients_remote_pending_nfiles_sem) == -1) {
            ERR_EXIT("sem_wait clients_remote_pending_nfiles_sem in "
                     "helper_nreads_req");
        }
        clients_remote_pending_nfiles[app_name].emplace_back(prefix_c, n_files, dest, files, sem);
        if (sem_post(&clients_remote_pending_nfiles_sem) == -1) {
            ERR_EXIT("sem_post clients_remote_pending_nfiles_sem in "
                     "helper_nreads_req");
        }
    }
    free(prefix);
    free(path_file);
    free(app_name);
}

void lightweight_MPI_Recv(char *buf_recv, int buf_size, MPI_Status *status) {
    START_LOG(gettid(), "call(buf_recv=0x%08x, buf_size=%d)", buf_recv, buf_size);
    MPI_Request request;
    int received = 0;
    MPI_Irecv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
              &request); // receive from server
    struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec  = 0;
    sleepTime.tv_nsec = 200000;

    while (!received) {
        MPI_Test(&request, &received, status);
        nanosleep(&sleepTime, &returnTime);
    }
    int bytes_received;
    MPI_Get_count(status, MPI_BYTE, &bytes_received);
}

void recv_file(char *shm, int source, long int bytes_expected) {
    START_LOG(gettid(), "call(%ld, %d, %ld)", shm, source, bytes_expected);
    MPI_Status status;
    int bytes_received;
    int count;
    for (long int k = 0; k < bytes_expected; k += bytes_received) {
        if (bytes_expected - k > 1024L * 1024 * 1024) {
            count = 1024L * 1024 * 1024;
        } else {
            count = bytes_expected - k;
        }
        MPI_Recv(shm + k, count, MPI_BYTE, source, 0, MPI_COMM_WORLD, &status);
        MPI_Get_count(&status, MPI_BYTE, &bytes_received);
    }
}

void serve_remote_stat(const char *path, int dest, const Capio_file &c_file) {
    START_LOG(gettid(), "call(%s, %d, %ld)", path, dest, c_file.get_buf_size());
    char msg[PATH_MAX + 1024];
    int dir;

    if (c_file.is_dir()) {
        dir = 0;
    } else {
        dir = 1;
    }
    off64_t size = c_file.get_file_size();
    sprintf(msg, "size %s %ld %d", path, size, dir);

    MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
}

void wait_for_completion(char *const path, const Capio_file &c_file, int dest, sem_t *sem) {
    START_LOG(gettid(), "call(path=%s, dest=%d)", path, dest);

    if (sem_wait(sem) == -1) {
        ERR_EXIT("sem_wait in wait_for_completion");
    }
    free(sem);

    serve_remote_stat(path, dest, c_file);
    free(path);
}

void helper_stat_req(const char *buf_recv) {
    START_LOG(gettid(), "call(%s)", buf_recv);
    char *path_c = (char *) malloc(sizeof(char) * PATH_MAX);
    if (path_c == nullptr) {
        ERR_EXIT("malloc 1 in helper_stat_req");
    }
    int dest;
    sscanf(buf_recv, "stat %d %s", &dest, path_c);

    Capio_file &c_file = get_capio_file(path_c);
    if (c_file.complete) {
        serve_remote_stat(path_c, dest, c_file);
    } else { // wait for completion
        sem_t *sem = (sem_t *) malloc(sizeof(sem_t));
        if (sem == nullptr) {
            ERR_EXIT("malloc 2 in helper_stat_req");
        }
        if (sem_init(sem, 0, 0) == -1) {
            ERR_EXIT("sem_init in helper_stat_req");
        }
        clients_remote_pending_stat[path_c].emplace_back(sem);
        std::thread t(wait_for_completion, path_c, std::cref(c_file), dest, sem);
        t.detach();
    }
    free(path_c);
}

void helper_handle_stat_reply(char *buf_recv) {
    START_LOG(gettid(), "call(buf_recv=%s)", buf_recv);

    char path_c[1024];
    off64_t size;
    int dir;
    sscanf(buf_recv, "size %s %ld %d", path_c, &size, &dir);
    stat_reply_request(path_c, size, dir);
}

void recv_nfiles(char *buf_recv, int source) {
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
            std::string node_name     = rank_to_node[source];
            const char *node_name_str = node_name.c_str();
            char *p_node_name         = (char *) malloc(sizeof(char) * (strlen(node_name_str) + 1));
            strcpy(p_node_name, node_name_str);
            add_file_location(path, p_node_name, -1);
            p_shm              = new char[file_size];
            Capio_file &c_file = create_capio_file(path, false, file_size);
            c_file.insert_sector(0, file_size);
            c_file.complete       = true;
            c_file.real_file_size = file_size;
            c_file.first_write    = false;
        }
        recv_file((char *) p_shm, source, file_size);
    }

    for (const auto &pair : files) {
        std::string file_path      = pair.first;
        std::string bytes_received = pair.second;
        solve_remote_reads(std::stol(bytes_received), 0, std::stol(bytes_received),
                           file_path.c_str(), true);
    }
}

void capio_helper() {
    START_LOG(gettid(), "call()");

    size_t buf_size = sizeof(char) * (PATH_MAX + 81920);
    char *buf_recv  = (char *) malloc(buf_size);
    if (buf_recv == nullptr) {
        ERR_EXIT("malloc 1 in capio_helper");
    }
    MPI_Status status;
    sem_wait(&internal_server_sem);
    while (true) {
#ifdef CAPIOSYNC
        MPI_Recv(buf_recv, buf_size, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD,
                 &status); // receive from server
#else
        lightweight_MPI_Recv(buf_recv, buf_size,
                             &status); // receive from server
#endif

        int source                  = status.MPI_SOURCE;
        bool remote_request_to_read = strncmp(buf_recv, "read", 4) == 0;
        if (remote_request_to_read) {
            // schema msg received: "read path dest offset nbytes"
            char *path_c = (char *) malloc(sizeof(char) * PATH_MAX);
            if (path_c == nullptr) {
                ERR_EXIT("malloc 2 in capio_helper");
            }
            int dest;
            long int offset, nbytes;
            sscanf(buf_recv, "read %s %d %ld %ld", path_c, &dest, &offset, &nbytes);

            // check if the data is available
            Capio_file &c_file  = get_capio_file(path_c);
            size_t file_size    = c_file.get_stored_size();
            bool complete       = c_file.complete;
            bool data_available = (offset + nbytes <= file_size);
            if (complete || (c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE && data_available)) {
                serve_remote_read(path_c, dest, offset, nbytes, complete);
            } else {
                auto *sem = (sem_t *) malloc(sizeof(sem_t));
                if (sem == nullptr) {
                    ERR_EXIT("malloc 4 in capio_helper");
                }
                if (sem_init(sem, 0, 0) == -1) {
                    ERR_EXIT("sem_init sem");
                }
                clients_remote_pending_reads[path_c].emplace_back(offset, nbytes, sem);
                std::thread t(wait_for_data, path_c, offset, dest, nbytes, sem);
                t.detach();
            }
        } else if (strncmp(buf_recv, "sending", 7) == 0) { // receiving a file
            off64_t bytes_received;
            int source = status.MPI_SOURCE;
            off64_t offset;
            char path_c[1024];
            int complete_tmp;
            size_t file_size;
            sscanf(buf_recv, "sending %s %ld %ld %d %zu", path_c, &offset, &bytes_received,
                   &complete_tmp, &file_size);
            bool complete = complete_tmp;
            std::string path(path_c);

            void *file_shm;
            Capio_file &c_file = init_capio_file(path.c_str(), file_shm);
            if (bytes_received != 0) {
                off64_t file_shm_size = c_file.get_buf_size();
                off64_t file_size     = offset + bytes_received;
                if (file_size > file_shm_size) {
                    file_shm = expand_memory_for_file(path, file_size, c_file);
                }
                recv_file((char *) file_shm + offset, source, bytes_received);
                bytes_received *= sizeof(char);
            }
            solve_remote_reads(bytes_received, offset, file_size, path.c_str(), complete);
        } else if (strncmp(buf_recv, "stat", 4) == 0) {
            helper_stat_req(buf_recv);
        } else if (strncmp(buf_recv, "size", 4) == 0) {
            helper_handle_stat_reply(buf_recv);
        } else if (strncmp(buf_recv, "nrea", 4) == 0) {
            helper_nreads_req(buf_recv, source);
        } else if (strncmp(buf_recv, "nsend", 5) == 0) {
            recv_nfiles(buf_recv, source);
        } else {
            logfile << "helper error receiving message" << std::endl;
        }
    }
}

int parseCLI(int argc, char **argv, int rank) {
    Logger *log;

    args::ArgumentParser parser(CAPIO_SERVER_ARG_PARSER_PRE, CAPIO_SERVER_ARG_PARSER_EPILOGUE);
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "help", "Display this help menu", {'h', "help"});
    args::ValueFlag<std::string> logfile_src(arguments, "filename",
                                             CAPIO_SERVER_ARG_PARSER_LOGILE_OPT_HELP, {'l', "log"});
    args::ValueFlag<std::string> config(arguments, "filename",
                                        CAPIO_SERVER_ARG_PARSER_CONFIG_OPT_HELP, {'c', "config"});
    args::Flag noConfigFile(arguments, "no-config",
                            CAPIO_SERVER_ARG_PARSER_CONFIG_NO_CONF_FILE_HELP, {"no-config"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &) {
        std::cout << CAPIO_SERVER_ARG_PARSER_PRE_COMMAND << parser;
        MPI_Finalize();
        exit(EXIT_SUCCESS);
    } catch (args::ParseError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        MPI_Finalize();
        exit(EXIT_FAILURE);
    } catch (args::ValidationError &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }
    if (logfile_src) {
        // log file was given
        std::string token = args::get(logfile_src);
        if (token.find(".log") != std::string::npos) {
            token.erase(token.length() - 4); // delete .log if for some reason
            // is given as parameter
        }

        std::string filename = token + "_" + std::to_string(rank) + ".log";
        logfile.open(filename, std::ofstream::out);
        log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "started logging to: " << filename << std::endl;
    } else {
        // log file not given. starting with default name
        const std::string logname(CAPIO_SERVER_DEFAULT_LOG_FILE_NAME + std::to_string(rank) +
                                  ".log");
        logfile.open(logname, std::ofstream::out);
        log = new Logger(__func__, __FILE__, __LINE__, gettid(), "Created new log file");
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "started logging to default logfile " << logname
                  << std::endl;
    }

    if (noConfigFile) {
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "skipping config file parsing" << std::endl;
    } else {
        if (config) {
            std::string token            = args::get(config);
            const std::string *capio_dir = get_capio_dir();
            std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "parsing config file: " << token
                      << std::endl;
            parse_conf_file(token, capio_dir);
        } else {
            std::cout
                << CAPIO_SERVER_CLI_LOG_SERVER_ERROR
                << "Error: no config file provided. To skip config file use --no-config option!"
                << std::endl;
#ifdef CAPIOLOG
            log->log("no config file provided, and  --no-config not provided");
#endif
            exit(EXIT_FAILURE);
        }
    }

    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "CAPIO_DIR=" << get_capio_dir()->c_str()
              << std::endl;

    delete log;

#ifdef CAPIO_LOG
    CAPIO_LOG_LEVEL = get_capio_log_level();
    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "LOG_LEVEL set to: " << CAPIO_LOG_LEVEL
              << std::endl;
    std::cout << CAPIO_LOG_CLI_WARNING;

#else
    if (std::getenv("CAPIO_LOG_LEVEL") != nullptr) {
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING
                  << CAPIO_LOG_CLI_WARNING_LOG_SET_NOT_COMPILED << std::endl;
    }
#endif

    std::cout << CAPIO_SERVER_CLI_LOG_SERVER << "server initialization completed!" << std::flush;
    return 0;
}

int main(int argc, char **argv) {
    int rank, len, provided;

    std::cout << CAPIO_BANNER;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    parseCLI(argc, argv, rank);

    START_LOG(gettid(), "call()");
    LOG("MPI_Comm_rank returned %d", rank);

    if (provided != MPI_THREAD_MULTIPLE) {
        LOG("The threading support level is lesser than that demanded");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
    MPI_Get_processor_name(node_name, &len);
    int res = sem_init(&internal_server_sem, 0, 0);
    if (res != 0) {
        ERR_EXIT("sem_init internal_server_sem failed with status %d", res);
    }
    if (sem_init(&remote_read_sem, 0, 1) == -1) {
        ERR_EXIT("sem_init remote_read_sem in main");
    }
    if (sem_init(&clients_remote_pending_nfiles_sem, 0, 1) == -1) {
        ERR_EXIT("sem_init clients_remote_pending_nfiles_sem in main");
    }
    std::thread server_thread(capio_server, rank);
    std::thread helper_thread(capio_helper);
    server_thread.join();
    helper_thread.join();
    res = sem_destroy(&internal_server_sem);
    if (res != 0) {
        MPI_Finalize();
        ERR_EXIT("sem_destroy internal_server_sem failed with status %d", res);
    }
    res = sem_destroy(&remote_read_sem);
    if (res != 0) {
        MPI_Finalize();
        ERR_EXIT("sem_destroy remote_read_sem failed with status %d", res);
    }
    MPI_Finalize();

    return 0;
}
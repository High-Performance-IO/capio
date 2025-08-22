#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include <utility>

#include "capio/requests.hpp"
#include <capio/queue.hpp>
#include <capio/response_queue.hpp>

#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

inline thread_local std::vector<std::regex> *paths_to_store_in_memory = nullptr;

inline CircularBuffer<char> *buf_requests;
inline std::unordered_map<long, ResponseQueue *> *bufs_response;

inline thread_local SPSCQueue *cts_queue;
inline thread_local SPSCQueue *stc_queue;

#include "cache.hpp"

/**
 * Initialize request and response buffers
 * @return
 */
inline void init_client() {
    buf_requests =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);
    bufs_response = new std::unordered_map<long, ResponseQueue *>();
}

/**
 * Perform handshake.
 * @param tid
 * @param pid
 * @param app_name
 */
inline void handshake_request(const long tid, const long pid, const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());

    cts_queue = new SPSCQueue("queue-" + std::to_string(tid) + ".cts", get_cache_lines(),
                              get_cache_line_size(), get_capio_workflow_name(), true);
    stc_queue = new SPSCQueue("queue-" + std::to_string(tid) + ".stc", get_cache_lines(),
                              get_cache_line_size(), get_capio_workflow_name(), true);
    LOG("Initialized data transfer queues");

    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);

#ifndef CAPIO_BUILD_TESTS
    LOG("Waiting for response from capio_server");
    /*
     * The handshake request must be blocking ONLY when not building tests. This is because when
     * starting unit tests, the binary is loaded with libcapio_posix.so underneath thus performing
     * a handshake request. If the handshake is blocking, then the capio_server binary cannot be
     * started as the whole process is waiting for a handshake.
     */
    if (bufs_response->at(pid)->read() == 0) {
        ERR_EXIT("Error: handshake request sent while capio_server is shutting down!");
    }
#endif

    LOG("Sent handshake request");
}

/**
 * File in memory requests: server returns the amount of paths that needs to be obtained from the
 * server to know which files are going to be treated during write and read operations inside memory
 * @param pid
 * @return
 */
inline std::vector<std::regex> *file_in_memory_request(const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(pid=%ld)", pid);
    char req[CAPIO_REQ_MAX_SIZE];

    sprintf(req, "%04d %ld ", CAPIO_REQUEST_QUERY_MEM_FILE, pid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent query for which file to store in memory");
    capio_off64_t files_to_read_from_queue = bufs_response->at(pid)->read();
    LOG("Need to read %llu files from data queues", files_to_read_from_queue);
    const auto regex_vector = new std::vector<std::regex>;
    for (capio_off64_t i = 0; i < files_to_read_from_queue; i++) {
        LOG("Reading file number %d", i);
        auto file = new char[PATH_MAX]{};
        stc_queue->read(file, PATH_MAX);
        LOG("Obtained path %s", file);

        if (file[0] == '*') {
            LOG("Obtained all file regex. converting it to be coherent with CAPIO paths");
            auto c_dir = get_capio_dir().string();
            memcpy(file, c_dir.c_str(), c_dir.length());
            memcpy(file + c_dir.size(), "/*", 2);
            LOG("Generated path relative to CAPIO_DIR: %s", file);
        }

        regex_vector->emplace_back(generateCapioRegex(file));
        delete[] file;
    }
    return regex_vector;
}

inline capio_off64_t posix_directory_committed_request(const long pid,
                                                       const std::filesystem::path &path,
                                                       char *token_path) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s)", path.c_str());
    char req[CAPIO_REQ_MAX_SIZE];

    sprintf(req, "%04d %ld %s ", CAPIO_REQUEST_POSIX_DIR_COMMITTED, pid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent query for directory committement");
    capio_off64_t path_len = bufs_response->at(pid)->read();
    LOG("Directory %s has the token length of %llu", path.c_str(), path_len);

    stc_queue->read(token_path, path_len);
    LOG("commit token path will exist at %s", token_path);
    return path_len;
}

// non blocking
inline void close_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    write_request_cache_fs->flush(tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_CLOSE, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// non blocking
inline void create_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_CREATE, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// non blocking
inline void exit_group_request(const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld)", tid);
    write_request_cache_fs->flush(tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// block until open is possible
inline void open_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    write_request_cache_fs->flush(tid);

    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    capio_off64_t res = bufs_response->at(tid)->read();
    LOG("Obtained from server %llu", res);
}

// non blocking
inline void rename_request(const std::filesystem::path &old_path,
                           const std::filesystem::path &new_path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(old=%s, new=%s, tid=%ld)", old_path.c_str(),
              new_path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s %s", CAPIO_REQUEST_RENAME, tid, old_path.c_str(), new_path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

#include "utils/storage.hpp"

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

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
 * Perform handshake. server returns the amount of paths that needs to be obtained from the server
 * to know which files are going to be treated during write and read operations inside memory
 * @param tid
 * @param pid
 * @param app_name
 */
inline void handshake_request(const long tid, const long pid, const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent handshake request");

    cts_queue = new SPSCQueue("queue-" + std::to_string(tid) + ".cts", get_cache_lines(),
                              get_cache_line_size());
    stc_queue = new SPSCQueue("queue-" + std::to_string(tid) + ".stc", get_cache_lines(),
                              get_cache_line_size());
    LOG("Initialized data transfer queues");
}

inline std::vector<std::regex> *file_in_memory_request(const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(pid=%ld)", pid);
    char req[CAPIO_REQ_MAX_SIZE];

    sprintf(req, "%04d %ld ", CAPIO_REQUEST_QUERY_MEM_FILE, pid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent query for which file to store in memory");
    capio_off64_t files_to_read_from_queue = bufs_response->at(pid)->read();
    LOG("Need to read %llu files from data queues", files_to_read_from_queue);
    const auto regex_vector = new std::vector<std::regex>;
    for (int i = 0; i < files_to_read_from_queue; i++) {
        LOG("Reading %d file", i);
        auto file = new char[CAPIO_MAX_SPSCQUEUE_ELEM_SIZE]{};
        stc_queue->read(file, CAPIO_MAX_SPSCQUEUE_ELEM_SIZE);
        LOG("Obtained path %s", file);
        regex_vector->emplace_back(generateCapioRegex(file));
        delete[] file;
    }
    return regex_vector;
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

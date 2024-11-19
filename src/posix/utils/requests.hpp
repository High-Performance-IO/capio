#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include <utility>

#include "capio/requests.hpp"

#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

inline CircularBuffer<char> *buf_requests;
inline CPBufResponse_t *bufs_response;

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
    bufs_response = new CPBufResponse_t();
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
    capio_off64_t files_to_read_from_queue = 0;
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent handshake request");

    cts_queue = new SPSCQueue("queue-" + app_name + ".cts", CAPIO_MAX_SPSQUEUE_ELEMS,
                              CAPIO_MAX_SPSCQUEUE_ELEM_SIZE);
    stc_queue = new SPSCQueue("queue-" + app_name + ".stc", CAPIO_MAX_SPSQUEUE_ELEMS,
                              CAPIO_MAX_SPSCQUEUE_ELEM_SIZE);
    LOG("Initialized data transfer queues");
}

// non blocking
inline void close_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    write_request_cache->flush(tid);
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
    write_request_cache->flush(tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// block until open is possible
inline void open_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    write_request_cache->flush(tid);

    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    capio_off64_t res;
    bufs_response->at(tid)->read(&res);
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

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include "capio/requests.hpp"

#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

inline CircularBuffer<char> *buf_requests;
inline CPBufResponse_t *bufs_response;

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
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_listener(long tid) {
    auto *p_buf_response = new CircularBuffer<off_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid),
                                                     CAPIO_REQ_BUFF_CNT, sizeof(off_t));
    bufs_response->insert(std::make_pair(tid, p_buf_response));
}

// Block until server allows for proceeding to a generic request
inline void consent_to_proceed_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_CONSENT, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
}

// non blocking
inline void close_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
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
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// non blocking
inline void handshake_anonymous_request(const long tid, const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld)", tid, pid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld", CAPIO_REQUEST_HANDSHAKE_ANONYMOUS, tid, pid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// non blocking
inline void handshake_named_request(const long tid, const long pid, const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

// block until open is possible
inline void open_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
}

// return amount of readable bytes
inline off64_t read_request(const std::filesystem::path &path, const off64_t end_of_Read,
                            const long tid, const long fd) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, end_of_Read=%ld, tid=%ld, fd=%ld)",
              path.c_str(), end_of_Read, tid, fd);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %s %ld %ld %ld", CAPIO_REQUEST_READ, path.c_str(), tid, fd, end_of_Read);
    LOG("Sending read request %s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    LOG("Response to request is %ld", res);
    return res;
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

// non blocking as write is not in the pre port of capio semantics
inline void write_request(const std::filesystem::path &path, const off64_t count, const long tid,
                          const long fd) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, count=%ld, tid=%ld)", path.c_str(), count,
              tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s %ld", CAPIO_REQUEST_WRITE, tid, fd, path.c_str(), count);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

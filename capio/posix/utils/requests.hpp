#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include "common/requests.hpp"

#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

inline thread_local CircularBuffer<char> *buf_requests;
inline thread_local CircularBuffer<off_t> *buff_response;

/**
 * Initialize request and response buffers
 * @return
 */
inline void init_client(const long tid) {
    buff_response = new CircularBuffer<off_t>(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid),
                                              CAPIO_REQ_BUFF_CNT, sizeof(off_t));
    buf_requests =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);
}

inline off64_t access_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_ACCESS, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void clone_request(const long parent_tid, const long child_tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(parent_tid=%ld, child_tid=%ld)", parent_tid,
              child_tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d  %ld %ld", CAPIO_REQUEST_CLONE, parent_tid, child_tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline void close_request(const int fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld)", fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_CLOSE, tid, fd);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t rename_request(const long tid, const std::filesystem::path &old_path,
                              const std::filesystem::path &newpath) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, old_path=%s, new_path=%s)", tid,
              old_path.c_str(), newpath.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %s %s %ld", CAPIO_REQUEST_RENAME, old_path.c_str(), newpath.c_str(), tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t create_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_CREATE, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t create_exclusive_request(const int fd, const std::filesystem::path &path,
                                        const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_CREATE_EXCLUSIVE, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void dup_request(const int old_fd, const int new_fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(old_fd=%ld, new_fd=%ld, tid)", old_fd, new_fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %d", CAPIO_REQUEST_DUP, tid, old_fd, new_fd);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline void exit_group_request(const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld)", tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t getdents_request(const int fd, const off64_t count, bool is64bit, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, is_64_bit=%s, tid=%ld)", fd,
              count, is64bit ? "true" : "false", tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", is64bit ? CAPIO_REQUEST_GETDENTS64 : CAPIO_REQUEST_GETDENTS,
            tid, fd, count);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void handshake_anonymous_request(const long tid, const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld)", tid, pid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld", CAPIO_REQUEST_HANDSHAKE_ANONYMOUS, tid, pid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline void handshake_named_request(const long tid, const long pid, const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline CPStatResponse_t fstat_request(const int fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld)", fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_FSTAT, tid, fd);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    // FIXME: these two reads don't work in multithreading
    off64_t file_size;
    buff_response->read(&file_size);
    off64_t is_dir;
    buff_response->read(&is_dir);
    return {file_size, is_dir};
}

inline off64_t mkdir_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_MKDIR, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t open_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t read_request(const int fd, const off64_t count, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, tid=%ld)", fd, count, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_READ, tid, fd, count);
    LOG("Sending read request %s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    LOG("Response to request is %ld", res);
    return res;
}

inline off64_t seek_data_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK_DATA, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t seek_end_request(const int fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld)", fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_SEEK_END, tid, fd);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res, is_dir;
    buff_response->read(&res);
    buff_response->read(&is_dir);
    return res;
}

inline off64_t seek_hole_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK_HOLE, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t seek_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline CPStatResponse_t stat_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_STAT, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t file_size;
    buff_response->read(&file_size);
    if (file_size == CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        return {CAPIO_POSIX_SYSCALL_REQUEST_SKIP, -1};
    }
    off64_t is_dir;
    LOG("Received file size = %d", file_size);
    buff_response->read(&is_dir);
    LOG("Received is_dir = %d", is_dir);
    return {file_size, is_dir};
}

inline off64_t unlink_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_UNLINK, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t rmdir_request(const std::filesystem::path &dir_path, long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(dir_path=%s, tid=%ld)", dir_path.c_str(), tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %s %ld", CAPIO_REQUEST_RMDIR, dir_path.c_str(), tid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void write_request(const int fd, const off64_t count, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, tid=%ld)", fd, count, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_WRITE, tid, fd, count);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

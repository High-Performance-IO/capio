#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include "capio/requests.hpp"

#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

int actual_num_writes = 1;

CPBufRequest_t *buf_requests;
CPBufResponse_t *bufs_response;

/**
 * Initialize request and response buffers
 * @return
 */
inline void init_client() {
    // TODO: replace number with constexpr
    buf_requests  = new CPBufRequest_t("circular_buffer", 1024 * 1024, CAPIO_REQUEST_MAX_SIZE,
                                       CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES);
    bufs_response = new CPBufResponse_t();
}

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_listener(long tid) {
    // TODO: replace numbers with constexpr
    auto *p_buf_response =
        new CircularBuffer<off_t>("buf_response_" + std::to_string(tid), 8 * 1024 * 1024,
                                  sizeof(off_t), CAPIO_SEM_TIMEOUT_NANOSEC, CAPIO_SEM_MAX_RETRIES);
    bufs_response->insert(std::make_pair(tid, p_buf_response));
}

inline off64_t access_request(const std::filesystem::path &path, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_ACCESS, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline void clone_request(const long parent_tid, const long child_tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d  %ld %ld", CAPIO_REQUEST_CLONE, parent_tid, child_tid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline void close_request(const int fd, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_CLOSE, tid, fd);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline off64_t rename_request(const long tid, const std::filesystem::path &old_path,
                              const std::filesystem::path &newpath) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %s %s %ld", CAPIO_REQUEST_RENAME, old_path.c_str(), newpath.c_str(), tid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t create_request(const int fd, const std::filesystem::path &path, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_CREATE, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t create_exclusive_request(const int fd, const std::filesystem::path &path,
                                        const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_CREATE_EXCLUSIVE, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline void dup_request(const int old_fd, const int new_fd, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %d", CAPIO_REQUEST_DUP, tid, old_fd, new_fd);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline void exit_group_request(const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline off64_t add_getdents_request(const int fd, const off64_t count, bool is64bit,
                                    const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", is64bit ? CAPIO_REQUEST_GETDENTS64 : CAPIO_REQUEST_GETDENTS,
            tid, fd, count);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline void handshake_anonymous_request(const long tid, const long pid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %ld", CAPIO_REQUEST_HANDSHAKE_ANONYMOUS, tid, pid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline void handshake_named_request(const long tid, const long pid, const std::string &app_name) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
}

inline CPStatResponse_t fstat_request(const int fd, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_FSTAT, tid, fd);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    // FIXME: these two reads don't work in multithreading
    off64_t file_size;
    bufs_response->at(tid)->read(&file_size);
    off64_t is_dir;
    bufs_response->at(tid)->read(&is_dir);
    return {file_size, is_dir};
}

inline off64_t mkdir_request(const std::filesystem::path &path, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_MKDIR, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t open_request(const int fd, const std::filesystem::path &path, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t read_request(const int fd, const off64_t count, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_READ, tid, fd, count);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t rename_request(const std::filesystem::path &oldpath,
                              const std::filesystem::path &newpath, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %s %s %ld", CAPIO_REQUEST_RENAME, oldpath.c_str(), newpath.c_str(), tid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t seek_data_request(const int fd, const off64_t offset, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK_DATA, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t seek_end_request(const int fd, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_SEEK_END, tid, fd);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res, is_dir;
    bufs_response->at(tid)->read(&res);
    bufs_response->at(tid)->read(&is_dir);
    return res;
}

inline off64_t seek_hole_request(const int fd, const off64_t offset, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK_HOLE, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t seek_request(const int fd, const off64_t offset, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %d %zu", CAPIO_REQUEST_SEEK, tid, fd, offset);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline CPStatResponse_t stat_request(const std::filesystem::path &path, const long tid) {
    START_LOG(tid, "call(path=%s)", path.c_str());
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_STAT, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t file_size;
    bufs_response->at(tid)->read(&file_size);
    off64_t is_dir;
    LOG("Received file size = %d", file_size);
    bufs_response->at(tid)->read(&is_dir);
    LOG("Received is_dir = %d", is_dir);
    return {file_size, is_dir};
}

inline off64_t unlink_request(const std::filesystem::path &path, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %ld %s", CAPIO_REQUEST_UNLINK, tid, path.c_str());
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline off64_t rmdir_request(const std::filesystem::path &dir_path, long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    sprintf(req, "%04d %s %ld", CAPIO_REQUEST_RMDIR, dir_path.c_str(), tid);
    buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
    off64_t res;
    bufs_response->at(tid)->read(&res);
    return res;
}

inline void write_request(const int fd, const off64_t count, const long tid) {
    char req[CAPIO_REQUEST_MAX_SIZE];
    int num_writes_batch = get_num_writes_batch(tid);
    long int offset      = get_capio_fd_offset(fd);
    set_capio_fd_offset(fd, offset + count);
    // FIXME: works only if there is only one writer at time for each file
    if (actual_num_writes == num_writes_batch) {
        sprintf(req, "%04d %ld %d %ld %ld", CAPIO_REQUEST_WRITE, tid, fd, offset, count);
        buf_requests->write(req, CAPIO_REQUEST_MAX_SIZE);
        actual_num_writes = 1;
    } else {
        ++(actual_num_writes);
    }
}

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

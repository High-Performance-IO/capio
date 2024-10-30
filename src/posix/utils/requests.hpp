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

    // TODO: use var to set cache size
    // TODO: also enable multithreading
    init_caches();
}

inline void attach_data_queue(const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(app_name=%s)", app_name.c_str());
    cts_queue =
        new SPSCQueue(app_name + ".cts", CAPIO_MAX_SPSQUEUE_ELEMS, CAPIO_MAX_SPSQUEUE_ELEMS);
    stc_queue =
        new SPSCQueue(app_name + ".stc", CAPIO_MAX_SPSQUEUE_ELEMS, CAPIO_MAX_SQSCQUEUE_ELEM_SIZE);
}

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void register_listener(long tid) {
    auto *p_buf_response = new CircularBuffer<capio_off64_t>(
        SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT, sizeof(capio_off64_t));
    bufs_response->insert(std::make_pair(tid, p_buf_response));
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

/**
 * Perform handshake. server returns the amount of paths that needs to be obtained from the server
 * to know which files are going to be treated during write and read operations inside memory
 * @param tid
 * @param pid
 */
inline void handshake_anonymous_request(const long tid, const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld)", tid, pid);
    char req[CAPIO_REQ_MAX_SIZE];
    capio_off64_t files_to_read_from_queue;
    sprintf(req, "%04d %ld %ld", CAPIO_REQUEST_HANDSHAKE_ANONYMOUS, tid, pid);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent handshake request");
    attach_data_queue(CAPIO_DEFAULT_APP_NAME);
    LOG("Reading number of paths to receive from server");
    bufs_response->at(tid)->read(&files_to_read_from_queue, sizeof(files_to_read_from_queue));
    LOG("Need to read %llu paths", files_to_read_from_queue);
    paths_to_store_in_memory = new std::vector<std::string>;
    for (int i = 0; i < files_to_read_from_queue; i++) {
        LOG("Reading %d file", i);
        auto file = new char[CAPIO_MAX_SQSCQUEUE_ELEM_SIZE]{};
        stc_queue->read(file, CAPIO_MAX_SQSCQUEUE_ELEM_SIZE);
        LOG("Obtained path %s", file);
        paths_to_store_in_memory->emplace_back(file);
        delete[] file;
    }
}
/**
 * Perform handshake. server returns the amount of paths that needs to be obtained from the server
 * to know which files are going to be treated during write and read operations inside memory
 * @param tid
 * @param pid
 * @param app_name
 */
inline void handshake_named_request(const long tid, const long pid, const std::string &app_name) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    capio_off64_t files_to_read_from_queue;
    sprintf(req, "%04d %ld %ld %s", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name.c_str());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    LOG("Sent handshake request");
    attach_data_queue(app_name);
    LOG("Reading number of paths to recive from server");
    bufs_response->at(tid)->read(&files_to_read_from_queue, sizeof(files_to_read_from_queue));
    LOG("Need to read %llu paths", files_to_read_from_queue);
    paths_to_store_in_memory = new std::vector<std::string>;
    for (int i = 0; i < files_to_read_from_queue; i++) {
        LOG("Reading %d file", i);
        auto file = new char[CAPIO_MAX_SQSCQUEUE_ELEM_SIZE]{};
        stc_queue->read(file, CAPIO_MAX_SQSCQUEUE_ELEM_SIZE);
        LOG("Obtained path %s", file);
        paths_to_store_in_memory->emplace_back(file);
        delete[] file;
    }
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

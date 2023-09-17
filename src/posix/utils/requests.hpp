#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include "capio/requests.hpp"

#include "types.hpp"

int actual_num_writes = 1;

static CPBufRequest_t* buf_requests;
static CPBufResponse_t* bufs_response;

static inline void init_client() {
    buf_requests = new CPBufRequest_t("circular_buffer", 1024 * 1024, sizeof(char) * 256);
    bufs_response = new CPBufResponse_t();
}

static inline void register_thread(long tid) {
    auto* p_buf_response = new Circular_buffer<off_t>("buf_response" + std::to_string(tid), 8 * 1024 * 1024, sizeof(off_t));
    bufs_response->insert(std::make_pair(tid, p_buf_response));
}

static inline off64_t access_request(const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %s", CAPIO_REQUEST_ACCESS, tid, path);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline void clone_request(const long parent_tid, const long child_tid) {
  char req[256];
  sprintf(req, "%s %ld %ld", CAPIO_REQUEST_CLONE, parent_tid, child_tid);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline void close_request(const int fd, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d", CAPIO_REQUEST_CLOSE, tid, fd);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline void create_request(const int fd, const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %s", CAPIO_REQUEST_CREATE, tid, fd, path);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline off64_t create_exclusive_request(const int fd, const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %s", CAPIO_REQUEST_CREATE_EXCLUSIVE, tid, fd, path);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline void dup_request(const int old_fd, const int new_fd, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %d", CAPIO_REQUEST_DUP, tid, old_fd, new_fd);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline void exit_group_request(const long tid) {
  char req[256];
  sprintf(req, "%s %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline off64_t getdents_request(const int fd, const off64_t count, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %ld", CAPIO_REQUEST_GETDENTS, tid, fd, count);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t getdents64_request(const int fd, const off64_t count, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %ld", CAPIO_REQUEST_GETDENTS64, tid, fd, count);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline void handshake_anonymous_request(const long tid, const long pid) {
  char req[256];
  sprintf(req, "%s %ld %ld", CAPIO_REQUEST_HANDSHAKE_ANONYMOUS, tid, pid);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline void handshake_named_request(const long tid, const long pid, const char * const app_name) {
  char req[256];
  sprintf(req, "%s %ld %ld %s", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline void fstat_request(const int fd, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d", CAPIO_REQUEST_FSTAT, tid, fd);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline off64_t mkdir_request(const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %s", CAPIO_REQUEST_MKDIR, tid, path);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline void open_request(const int fd, const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %s", CAPIO_REQUEST_OPEN, tid, fd, path);
  buf_requests->write(req, 256 * sizeof(char));
}

static inline off64_t read_request(const int fd, const off64_t count, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %ld", CAPIO_REQUEST_READ, tid, fd, count);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t rename_request(const char * const oldpath, const char * const newpath, const long tid) {
  char req[256];
  sprintf(req, "%s %s %s %ld", CAPIO_REQUEST_RENAME, oldpath, newpath, tid);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t seek_data_request(const int fd, const off64_t offset, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %zu", CAPIO_REQUEST_SEEK_DATA, tid, fd, offset);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t seek_end_request(const int fd, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d", CAPIO_REQUEST_SEEK_END, tid, fd);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t seek_hole_request(const int fd, const off64_t offset, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %zu", CAPIO_REQUEST_SEEK_HOLE, tid, fd, offset);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline off64_t seek_request(const int fd, const off64_t offset, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %d %zu", CAPIO_REQUEST_SEEK, tid, fd, offset);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline CPStatResponse_t stat_request(const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %s", CAPIO_REQUEST_STAT, tid, path);
  buf_requests->write(req, 256 * sizeof(char));
  //FIXME: these two reads don't work in multithreading
  off64_t file_size;
  bufs_response->at(tid)->read(&file_size);
  off64_t is_dir;
  bufs_response->at(tid)->read(&is_dir);
  return std::make_tuple(file_size, is_dir);
}

static inline off64_t unlink_request(const char * const path, const long tid) {
  char req[256];
  sprintf(req, "%s %ld %s", CAPIO_REQUEST_UNLINK, tid, path);
  buf_requests->write(req, 256 * sizeof(char));
  off64_t res;
  bufs_response->at(tid)->read(&res);
  return res;
}

static inline void write_request(CPFiles_t * const files, const int fd, const off64_t count, const long tid) {
  char req[256];
  int num_writes_batch = get_num_writes_batch();
  *std::get<0>((*files)[fd]) += count;
    // FIXME: works only if there is only one writer at time for each file
  if (actual_num_writes == num_writes_batch) {
    long int old_offset = *std::get<0>((*files)[fd]);
    sprintf(req, "%s %ld %d %ld %ld", CAPIO_REQUEST_WRITE, tid, fd, old_offset, count);
    buf_requests->write(req, 256 * sizeof(char));
    actual_num_writes = 1;
  } else {
    ++(actual_num_writes);
  }
}

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

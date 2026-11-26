#ifndef CAPIO_POSIX_UTILS_REQUESTS_HPP
#define CAPIO_POSIX_UTILS_REQUESTS_HPP

#include "common/requests.hpp"

#include "calf/SyscallLogger.h"
#include "env.hpp"
#include "filesystem.hpp"
#include "types.hpp"

/**
 * The responses of the server. It refuses clients of the other variant, as the two keep files and
 * commits in different places and would wait for each other forever. The handshake cannot wait
 * for an answer, so a refused client learns it at the first request that has one.
 */
class ResponseBuffer : public CircularBuffer<off_t> {
  public:
    using CircularBuffer<off_t>::CircularBuffer;

    void read(off_t *response) {
        CircularBuffer<off_t>::read(response);
        if (*response == CAPIO_REPLY_VARIANT_REFUSED) {
            constexpr char message[] =
                MEMORY_MODE("CAPIO: this process uses libcapio_posix.so (memory), but its "
                            "workflow uses libcapio_posix_fs.so: the two cannot be mixed in a "
                            "workflow. Fix LD_PRELOAD.\n")
                    FS_MODE("CAPIO: this process uses libcapio_posix_fs.so (file system), but "
                            "its workflow uses libcapio_posix.so: the two cannot be mixed in a "
                            "workflow. Fix LD_PRELOAD.\n");
            syscall_no_intercept(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
            syscall_no_intercept(SYS_exit_group, EXIT_FAILURE);
        }
    }
};

inline thread_local CircularBuffer<char> *buf_requests;
inline thread_local ResponseBuffer *buff_response;

/**
 * Initialize request and response buffers
 * @return
 */
inline void init_client(const long tid) {
    buff_response =
        new ResponseBuffer(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid), CAPIO_REQ_BUFF_CNT,
                           sizeof(off_t), get_capio_workflow_name());
    buf_requests = new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT,
                                            CAPIO_REQ_MAX_SIZE, get_capio_workflow_name());
}

/**
 * Send a request that carries a path (open, stat, mkdir, rename, ...), in either variant.
 *
 * The request is "<code> <tid> <arguments><path>". The path goes last, so the server can read
 * everything after the arguments as the path, spaces included.
 *
 * A request is only CAPIO_REQ_MAX_SIZE (256) bytes long, and a path can be longer. In that case
 * the beginning of the path is sent first, split into as many CAPIO_REQUEST_PATH_PART requests as
 * needed, and the request itself carries only the end of the path. The server stores the parts it
 * receives from each thread and glues them back together when the request arrives. Each part
 * holds what is left of the 256 bytes after "<code> <tid> ", about 240 bytes. For example, a
 * 600-byte path for an open becomes three requests:
 *
 *   PATH_PART <tid> <first ~244 bytes of the path>
 *   PATH_PART <tid> <next ~244 bytes>
 *   OPEN      <tid> <fd> <the last ~112 bytes>
 *
 * Requests on an open file do not need this: they send the descriptor, and the server already
 * knows which path it belongs to.
 *
 * @param code
 * @param tid
 * @param arguments the arguments before the path, ending with a space
 * @param path
 */
inline void path_request(const int code, const long tid, const std::string &arguments,
                         const std::string_view path) {
    char req[CAPIO_REQ_MAX_SIZE];
    const size_t part_header =
        snprintf(req, sizeof(req), "%04d %ld ", CAPIO_REQUEST_PATH_PART, tid);
    const size_t header = part_header + arguments.size();

    std::string_view rest = path;
    while (header + rest.size() >= sizeof(req)) {
        const auto chunk = rest.substr(0, sizeof(req) - part_header - 1);
        snprintf(req, sizeof(req), "%04d %ld %.*s", CAPIO_REQUEST_PATH_PART, tid,
                 static_cast<int>(chunk.size()), chunk.data());
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        rest.remove_prefix(chunk.size());
    }
    snprintf(req, sizeof(req), "%04d %ld %s%.*s", code, tid, arguments.c_str(),
             static_cast<int>(rest.size()), rest.data());
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t access_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    path_request(CAPIO_REQUEST_ACCESS, tid, "", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void clone_request(const long parent_tid, const long child_tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(parent_tid=%ld, child_tid=%ld)", parent_tid,
              child_tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d  %ld %ld", CAPIO_REQUEST_CLONE, parent_tid, child_tid);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

/**
 * @brief Tell the server that @p tid closed @p fd
 * @param error FS variant: what the close failed with, 0 if it worked
 */
inline void close_request(const int fd, const long tid, const long error = 0) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld, error=%ld)", fd, tid, error);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_CLOSE, tid, fd, error);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t rename_request(const long tid, const std::filesystem::path &old_path,
                              const std::filesystem::path &newpath) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, old_path=%s, new_path=%s)", tid,
              old_path.c_str(), newpath.c_str());
    // Both names in one request, written one after the other with no separator (a path can contain
    // any character). The length of the old name, sent first, tells the server where it ends. As
    // in rename_request_fs.
    path_request(CAPIO_REQUEST_RENAME, tid, std::to_string(old_path.native().size()) + " ",
                 old_path.native() + newpath.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void rename_request_fs(const std::filesystem::path &old_path, const bool old_is_capio,
                              const std::filesystem::path &new_path, const bool new_is_capio,
                              const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, old_path=%s, new_path=%s)", tid,
              old_path.c_str(), new_path.c_str());
    // Both names go back to back. The length of the old name tells the server where it ends; a
    // separator would not work, since paths can contain spaces
    path_request(CAPIO_REQUEST_RENAME_FS, tid,
                 std::to_string(old_is_capio ? 1 : 0) + " " + std::to_string(new_is_capio ? 1 : 0) +
                     " " + std::to_string(old_path.native().size()) + " ",
                 old_path.native() + new_path.native());
}

inline off64_t create_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    path_request(CAPIO_REQUEST_CREATE, tid, std::to_string(fd) + " ", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

/**
 * @brief Tell the server that @p tid got the descriptor @p fd of @p path, or created @p path
 * @param fd -1 if none (mkdir)
 * @param produced whether that makes @p tid a producer of @p path: a CAPIO_FS_* value
 */
inline void create_request_fs(const std::filesystem::path &path, const int fd, const int produced,
                              const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, fd=%d, produced=%d, tid=%ld)", path.c_str(),
              fd, produced, tid);
    path_request(CAPIO_REQUEST_CREATE_FS, tid,
                 std::to_string(fd) + " " + std::to_string(produced) + " ", path.native());
    // The server answers a new producer, so that a client of the wrong variant stops before it
    // writes. The other ones get no answer: they are also sent while a program starts, maybe
    // before its server does.
    if (produced == CAPIO_FS_PRODUCED) {
        off64_t res;
        buff_response->read(&res);
    }
}

inline off64_t create_exclusive_request(const int fd, const std::filesystem::path &path,
                                        const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    path_request(CAPIO_REQUEST_CREATE_EXCLUSIVE, tid, std::to_string(fd) + " ", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void dup_request(const int old_fd, const int new_fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(old_fd=%ld, new_fd=%ld, tid)", old_fd, new_fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %d", CAPIO_REQUEST_DUP, tid, old_fd, new_fd);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline void exit_group_request(const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld)", tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld", CAPIO_REQUEST_EXIT_GROUP, tid);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t getdents_request(const int fd, const off64_t count, bool is64bit, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, is_64_bit=%s, tid=%ld)", fd,
              count, is64bit ? "true" : "false", tid);
    char req[CAPIO_REQ_MAX_SIZE];
    // The offset is for the FS server, which reads the directory on the file system: the memory
    // server knows it already
    sprintf(req, "%04d %ld %d %ld %ld", is64bit ? CAPIO_REQUEST_GETDENTS64 : CAPIO_REQUEST_GETDENTS,
            tid, fd, count, fd_table->offset(fd));
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void handshake_named_request(const long tid, const long pid, const std::string &app_name,
                                    const bool wait = false) {
    START_LOG(capio_syscall(SYS_gettid), "call(tid=%ld, pid=%ld, app_name=%s)", tid, pid,
              app_name.c_str());
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %ld %s %d %c", CAPIO_REQUEST_HANDSHAKE_NAMED, tid, pid, app_name.c_str(),
            wait ? 1 : 0, MEMORY_MODE(CAPIO_VARIANT_MEMORY) FS_MODE(CAPIO_VARIANT_FS));
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);

    if (wait) {
        off64_t res;
        buff_response->read(&res);
    }
}

inline CPStatResponse_t fstat_request(const int fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld)", fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_FSTAT, tid, fd);
    LOG("req=%s", req);
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
    path_request(CAPIO_REQUEST_MKDIR, tid, "", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t open_request(const int fd, const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, path=%s, tid=%ld)", fd, path.c_str(), tid);
    path_request(CAPIO_REQUEST_OPEN, tid, std::to_string(fd) + " ", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

/**
 * Ask the server to copy up to @count bytes from @fd_in at @offset_in to @fd_out at @offset_out,
 * two CAPIO descriptors. The server copies at most up to the end of the data that follows
 * @offset_in, and does not move the file offsets of the descriptors.
 *
 * @param fd_in
 * @param fd_out
 * @param offset_in
 * @param offset_out
 * @param count
 * @param tid
 * @return the number of bytes copied, 0 at the end of the input, or
 * CAPIO_POSIX_SYSCALL_REQUEST_SKIP if the server cannot do the copy and the client must do it
 */
inline off64_t copy_file_range_request(const int fd_in, const int fd_out, const off64_t offset_in,
                                       const off64_t offset_out, const off64_t count,
                                       const long tid) {
    START_LOG(capio_syscall(SYS_gettid),
              "call(fd_in=%d, fd_out=%d, offset_in=%ld, offset_out=%ld, count=%ld, tid=%ld)", fd_in,
              fd_out, offset_in, offset_out, count, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %d %ld %ld %ld", CAPIO_REQUEST_COPY_FILE_RANGE, tid, fd_in, fd_out,
            offset_in, offset_out, count);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    LOG("Response to request is %ld", res);
    return res;
}

inline bool open_request_fs(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    path_request(CAPIO_REQUEST_OPEN_FS, tid, "", path.native());
    off64_t res;
    buff_response->read(&res);
    return res != 0;
}

inline off64_t read_request(const int fd, const off64_t count, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, tid=%ld)", fd, count, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_READ, tid, fd, count);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    LOG("Response to request is %ld", res);
    return res;
}

inline off64_t seek_data_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_SEEK_DATA, tid, fd, offset);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t seek_end_request(const int fd, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, tid=%ld)", fd, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d", CAPIO_REQUEST_SEEK_END, tid, fd);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res, is_dir;
    buff_response->read(&res);
    buff_response->read(&is_dir);
    return res;
}

inline off64_t seek_hole_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_SEEK_HOLE, tid, fd, offset);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t seek_request(const int fd, const off64_t offset, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, offset=%ld, tid=%ld)", fd, offset, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_SEEK, tid, fd, offset);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline CPStatResponse_t stat_request(const std::filesystem::path &path, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld)", path.c_str(), tid);
    path_request(CAPIO_REQUEST_STAT, tid, "", path.native());
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
    path_request(CAPIO_REQUEST_UNLINK, tid, "", path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline off64_t rmdir_request(const std::filesystem::path &dir_path, long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(dir_path=%s, tid=%ld)", dir_path.c_str(), tid);
    path_request(CAPIO_REQUEST_RMDIR, tid, "", dir_path.native());
    off64_t res;
    buff_response->read(&res);
    return res;
}

inline void write_request(const int fd, const off64_t count, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%ld, count=%ld, tid=%ld)", fd, count, tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_WRITE, tid, fd, count);
    LOG("req=%s", req);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
}

inline off64_t consent_to_proceed_request(const std::filesystem::path &path, const long tid,
                                          const std::string &source_func) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source_func=%s)", path.c_str(),
              tid, source_func.c_str());
    path_request(CAPIO_REQUEST_CONSENT, tid, "", path.native());
    off64_t res;
    buff_response->read(&res);
    LOG("Obtained from server %ld", res);
    return res;
}

/// @brief Ask the server how many bytes of the file open as @p fd can be read, up to @p end_of_read
/// (waiting for them if needed). Only the descriptor is sent: the server learned its path when
/// the file was opened (CAPIO_REQUEST_CREATE_FS).
inline off64_t read_request_fs(const int fd, const off64_t end_of_read, const long tid) {
    START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, end_of_read=%ld, tid=%ld)", fd, end_of_read,
              tid);
    char req[CAPIO_REQ_MAX_SIZE];
    sprintf(req, "%04d %ld %d %ld", CAPIO_REQUEST_READ_FS, tid, fd, end_of_read);
    buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
    off64_t res;
    buff_response->read(&res);
    LOG("Response to request is %ld", res);
    return res;
}

#endif // CAPIO_POSIX_UTILS_REQUESTS_HPP

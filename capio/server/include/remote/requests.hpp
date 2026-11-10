#ifndef CAPIO_REMOTE_REQUESTS_HPP
#define CAPIO_REMOTE_REQUESTS_HPP

#include "remote/home_node_service.hpp"
#include "storage/manager.hpp"
#include "utils/runtime_configuration.hpp"

extern StorageManager *storage_manager;
extern Backend *backend;

inline void serve_remote_stat_request(const std::filesystem::path &path, int source_tid,
                                      off64_t file_size, bool is_dir, const std::string &dest) {
    const char *const format = "%04d %s %d %ld %d";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_STAT_REPLY, path.c_str(),
                              source_tid, file_size, is_dir);
    const std::unique_ptr<char[]> message(new char[size + 1]);

    sprintf(message.get(), "%04d %s %d %ld %d", CAPIO_SERVER_REQUEST_STAT_REPLY, path.c_str(),
            source_tid, file_size, is_dir);

    backend->send_request(message.get(), size + 1, dest);
}

inline off64_t remote_reply_size(const CapioFile &c_file, const off64_t offset) {
    const off64_t nbytes             = std::max<off64_t>(0, c_file.getStoredSize() - offset);
    const off64_t prefetch_data_size = RuntimeConfiguration::get().getPrefetchDataSize();
    return prefetch_data_size != 0 && nbytes > prefetch_data_size ? prefetch_data_size : nbytes;
}

inline void serve_remote_read_request(int tid, int fd, int count, long int nbytes,
                                      const off64_t file_size, bool complete, bool is_getdents,
                                      const std::string &dest, char *file) {
    START_LOG(gettid(), "call()");
    const char *const format = "%04d %d %d %d %ld %ld %d %d";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd, count,
                              nbytes, file_size, complete, is_getdents);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd, count, nbytes,
            file_size, complete, is_getdents);
    LOG("Message = %s", message.get());

    backend->send_file(message.get(), size + 1, file, nbytes, dest);
}

/// @brief Ask @p dest, the home of @p path, for its size and kind on behalf of client @p tid
inline void handle_remote_stat_request(int tid, const std::filesystem::path &path,
                                       const std::string &dest) {
    START_LOG(gettid(), "call(tid=%d, path=%s, dest=%s)", tid, path.c_str(), dest.c_str());

    const char *const format = "%04d %d %s %s";
    const int size           = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_STAT, tid,
                                        backend->get_node_name().c_str(), path.c_str());
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_STAT, tid, backend->get_node_name().c_str(),
            path.c_str());
    LOG("destination=%s, message=%s", dest.c_str(), message.get());

    backend->send_request(message.get(), size + 1, dest);
    LOG("message sent");
}

/// @brief Ask @p dest, the home of the file open as @p fd, for the data a read or getdents needs
inline void handle_remote_read_request(int tid, int fd, off64_t count, bool is_getdents,
                                       const std::string &dest) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_getdents=%s, dest=%s)", tid, fd, count,
              is_getdents ? "true" : "false", dest.c_str());

    // If it is not in cache then send the request to the remote node
    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    off64_t offset                    = storage_manager->getFileOffset(tid, fd);

    const char *const format = "%04d %s %d %d %ld %ld %d";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid, fd,
                              count, offset, is_getdents);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid, fd, count, offset,
            is_getdents);

    LOG("Message = %s", message.get());
    backend->send_request(message.get(), size + 1, dest);
}

inline void handle_remote_copy_file_range_request(const int tid, const int fd_in, const int fd_out,
                                                  const std::filesystem::path &path_in,
                                                  const off64_t offset_in, const off64_t offset_out,
                                                  const off64_t count, const std::string &dest) {
    START_LOG(gettid(),
              "call(tid=%d, fd_in=%d, fd_out=%d, path_in=%s, offset_in=%ld, count=%ld, dest=%s)",
              tid, fd_in, fd_out, path_in.c_str(), offset_in, count, dest.c_str());

    const char *const format = "%04d %s %d %d %d %ld %ld %ld";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_COPY_FILE_RANGE,
                              path_in.c_str(), tid, fd_in, fd_out, offset_in, offset_out, count);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_COPY_FILE_RANGE, path_in.c_str(), tid,
            fd_in, fd_out, offset_in, offset_out, count);

    LOG("destination=%s, message=%s", dest.c_str(), message.get());
    backend->send_request(message.get(), size + 1, dest);
}

inline void serve_remote_copy_file_range_request(const int tid, const int fd_in, const int fd_out,
                                                 const off64_t offset_in, const off64_t offset_out,
                                                 const off64_t count, const off64_t nbytes,
                                                 const off64_t file_size, const bool complete,
                                                 const std::string &dest, char *data) {
    START_LOG(gettid(), "call(tid=%d, offset_in=%ld, nbytes=%ld, dest=%s)", tid, offset_in, nbytes,
              dest.c_str());

    const char *const format = "%04d %d %d %d %ld %ld %ld %ld %ld %d";
    const int size =
        snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_COPY_FILE_RANGE_REPLY, tid, fd_in, fd_out,
                 offset_in, offset_out, count, nbytes, file_size, complete);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_COPY_FILE_RANGE_REPLY, tid, fd_in, fd_out,
            offset_in, offset_out, count, nbytes, file_size, complete);

    LOG("Message = %s", message.get());
    backend->send_file(message.get(), size + 1, data, nbytes, dest);
}

#endif // CAPIO_REMOTE_REQUESTS_HPP

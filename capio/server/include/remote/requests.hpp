#ifndef CAPIO_REMOTE_REQUESTS_HPP
#define CAPIO_REMOTE_REQUESTS_HPP

#include "storage/storage_service.hpp"
extern StorageService *storage_service;

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

inline void serve_remote_read_request(int tid, int fd, int count, long int nbytes,
                                      const off64_t file_size, bool complete, bool is_getdents,
                                      const std::string &dest) {
    START_LOG(gettid(), "call()");
    const char *const format = "%04d %d %d %d %ld %ld %d %d";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd, count,
                              nbytes, file_size, complete, is_getdents);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ_REPLY, tid, fd, count, nbytes,
            file_size, complete, is_getdents);
    LOG("Message = %s", message.get());

    // send request
    backend->send_request(message.get(), size + 1, dest);
}

inline void send_files_batch_request(const std::string &prefix, int tid, int fd, int count,
                                     bool is_getdents, const std::string &dest,
                                     const std::vector<std::string> *files_to_send) {
    START_LOG(gettid(), "call()");
    const char *const format = "%04d %s %d %d %d %d";
    const int size           = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_BATCH_REPLY,
                                        prefix.c_str(), tid, fd, count, is_getdents);
    const std::unique_ptr<char[]> header(new char[size + 1]);
    sprintf(header.get(), format, CAPIO_SERVER_REQUEST_READ_BATCH_REPLY, prefix.c_str(), tid, fd,
            count, is_getdents);
    std::string message(header.get());
    for (const std::string &path : *files_to_send) {
        CapioFile &c_file = storage_service->get(path);
        message.append(" " + path.substr(prefix.length()) + " " +
                       std::to_string(c_file.get_stored_size()));
    }
    LOG("Message = %s", message.c_str());

    // send request
    backend->send_request(message.c_str(), message.length(), dest);
}

inline void handle_remote_stat_request(int tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    std::string dest         = std::get<0>(get_file_location(path));
    const char *const format = "%04d %d %s %s";
    const int size =
        snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_STAT, tid, node_name, path.c_str());
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_STAT, tid, node_name, path.c_str());
    LOG("destination=%s, message=%s", dest.c_str(), message.get());

    backend->send_request(message.get(), size + 1, dest);
    LOG("message sent");
}

inline void handle_remote_read_batch_request(int tid, int fd, off64_t count,
                                             const std::string &app_name, const std::string &prefix,
                                             off64_t batch_size, bool is_getdents) {
    START_LOG(gettid(),
              "call(tid=%d, fd=%d, count=%ld, app_name=%s, prefix=%s, "
              "batch_size=%ld, is_getdents=%s)",
              tid, fd, count, app_name.c_str(), prefix.c_str(), batch_size,
              is_getdents ? "true" : "false");

    const std::filesystem::path &path = storage_service->getPath(tid, fd);
    std::string dest                  = std::get<0>(get_file_location(path));

    const char *const format = "%04d %s %d %d %ld %ld %s %s %d";
    const int size =
        snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ_BATCH, path.c_str(), tid, fd, count,
                 batch_size, app_name.c_str(), prefix.c_str(), is_getdents);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ_BATCH, path.c_str(), tid, fd, count,
            batch_size, app_name.c_str(), prefix.c_str(), is_getdents);
    LOG("Message = %s", message.get());
    backend->send_request(message.get(), size + 1, dest);
}

inline void handle_remote_read_request(int tid, int fd, off64_t count, bool is_getdents) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld, is_getdents=%s)", tid, fd, count,
              is_getdents ? "true" : "false");

    // If it is not in cache then send the request to the remote node
    const std::filesystem::path &path = storage_service->getPath(tid, fd);
    off64_t offset                    = storage_service->getFileOffset(tid, fd);
    std::string dest                  = std::get<0>(get_file_location(path));

    const char *const format = "%04d %s %d %d %ld %ld %d";
    const int size = snprintf(nullptr, 0, format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid, fd,
                              count, offset, is_getdents);
    const std::unique_ptr<char[]> message(new char[size + 1]);
    sprintf(message.get(), format, CAPIO_SERVER_REQUEST_READ, path.c_str(), tid, fd, count, offset,
            is_getdents);

    LOG("Message = %s", message.get());
    backend->send_request(message.get(), size + 1, dest);
}

#endif // CAPIO_REMOTE_REQUESTS_HPP

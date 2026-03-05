#ifndef CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "storage/manager.hpp"

extern StorageManager *storage_manager;

inline void serve_remote_read(const std::filesystem::path &path, const std::string &dest, int tid,
                              int fd, off64_t count, off64_t offset, bool complete,
                              bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, dest=%s, tid=%d, fd=%d, count=%ld, offset=%ld, complete=%s, "
              "is_getdents=%s)",
              path.c_str(), dest.c_str(), tid, fd, count, offset, complete ? "true" : "false",
              is_getdents ? "true" : "false");

    // Send all the rest of the file not only the number of bytes requested
    // Useful for caching
    CapioFile &c_file          = storage_manager->get(path);
    long int nbytes            = c_file.get_stored_size() - offset;
    off64_t prefetch_data_size = get_prefetch_data_size();

    if (prefetch_data_size != 0 && nbytes > prefetch_data_size) {
        nbytes = prefetch_data_size;
    }
    const off64_t file_size = c_file.get_stored_size();

    // send request
    serve_remote_read_request(tid, fd, count, nbytes, file_size, complete, is_getdents, dest);
    // send data
    backend->send_file(c_file.get_buffer() + offset, nbytes, dest);
}

inline void handle_read_reply(int tid, int fd, long count, off64_t file_size, off64_t nbytes,
                              bool complete, bool is_getdents) {
    START_LOG(
        gettid(),
        "call(tid=%d, fd=%d, count=%ld, file_size=%ld, nbytes=%ld, complete=%s, is_getdents=%s)",
        tid, fd, count, file_size, nbytes, complete ? "true" : "false",
        is_getdents ? "true" : "false");

    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);
    off64_t offset                    = storage_manager->getFileOffset(tid, fd);
    c_file.real_file_size             = file_size;
    c_file.insert_sector(offset, offset + nbytes);
    c_file.set_complete(complete);

    off64_t end_of_sector = c_file.get_sector_end(offset);
    c_file.create_buffer_if_needed(path, false);
    off64_t bytes_read;
    off64_t end_of_read = offset + count;
    if (end_of_sector > end_of_read) {
        end_of_sector = end_of_read;
        bytes_read    = count;
    } else {
        bytes_read = end_of_sector - offset;
    }
    if (is_getdents) {
        send_dirent_to_client(tid, fd, c_file, offset, bytes_read);
    } else {
        client_manager->replyToClient(tid, offset, c_file.get_buffer(), count);
        storage_manager->setFileOffset(tid, fd, offset + count);
    }
}

void wait_for_data(const std::filesystem::path &path, const std::string &dest, int tid, int fd,
                   off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, dest=%s, tid=%d, fs=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), dest.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    const CapioFile &c_file = storage_manager->get(path);
    // wait that nbytes are written
    c_file.wait_for_data(offset + count);
    serve_remote_read(path, dest, tid, fd, count, offset, c_file.is_complete(), is_getdents);
}

inline void handle_remote_read(const std::filesystem::path &path, const std::string &source,
                               int tid, int fd, off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, source=%s, tid=%d, fd=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), source.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    CapioFile &c_file   = storage_manager->get(path);
    bool data_available = (offset + count <= c_file.get_stored_size());
    if (c_file.is_complete() || (CapioCLEngine::get().isFirable(path) && data_available)) {
        serve_remote_read(path, source, tid, fd, count, offset, c_file.is_complete(), is_getdents);
    } else {
        std::thread t(wait_for_data, path, source, tid, fd, count, offset, is_getdents);
        t.detach();
    }
}

inline void handle_remote_read_reply(const std::string &source, int tid, int fd, off64_t count,
                                     off64_t nbytes, off64_t file_size, bool complete,
                                     bool is_getdents) {
    START_LOG(gettid(),
              "call(source=%s, tid=%d, fd=%d, count=%ld, nbytes=%ld, file_size=%ld, complete=%s, "
              "is_getdents=%s)",
              source.c_str(), tid, fd, count, nbytes, file_size, complete ? "true" : "false",
              is_getdents ? "true" : "false");

    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    off64_t offset                    = storage_manager->getFileOffset(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);

    c_file.create_buffer_if_needed(path, false);
    if (nbytes != 0) {
        auto file_shm_size  = c_file.get_buf_size();
        auto file_size_recv = offset + nbytes;
        if (file_size_recv > file_shm_size) {
            c_file.expand_buffer(file_size_recv);
        }
        c_file.read_from_node(source, offset, nbytes);
        nbytes *= sizeof(char);
    }
    handle_read_reply(tid, fd, count, file_size, nbytes, complete, is_getdents);
}

void remote_read_handler(const RemoteRequest &request) {
    const std::string &dest = request.get_source();
    char path[PATH_MAX];
    int tid, fd, is_getdents;
    off64_t count, offset;
    sscanf(request.get_content(), "%s %d %d %ld %ld %d", path, &tid, &fd, &count, &offset,
           &is_getdents);
    handle_remote_read(path, dest, tid, fd, count, offset, is_getdents);
}

void remote_read_reply_handler(const RemoteRequest &request) {
    const std::string &dest = request.get_source();
    off64_t count, nbytes, file_size;
    int tid, fd, complete, is_getdents;
    sscanf(request.get_content(), "%d %d %ld %ld %ld %d %d", &tid, &fd, &count, &nbytes, &file_size,
           &complete, &is_getdents);
    handle_remote_read_reply(dest, tid, fd, count, nbytes, file_size, complete, is_getdents);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

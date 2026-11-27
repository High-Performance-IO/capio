#ifndef CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

#include <algorithm>

#include "calf/StlLogger.h"
#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "storage/manager.hpp"
#include "utils/common.hpp"
#include "utils/runtime_configuration.hpp"

extern StorageManager *storage_manager;

inline void serve_remote_read(const std::filesystem::path &path, const std::string &dest, int tid,
                              int fd, off64_t count, off64_t offset, bool complete,
                              bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, dest=%s, tid=%d, fd=%d, count=%ld, offset=%ld, complete=%s, "
              "is_getdents=%s)",
              path.c_str(), dest.c_str(), tid, fd, count, offset, complete ? "true" : "false",
              is_getdents ? "true" : "false");

    CapioFile &c_file = storage_manager->get(path);
    // The backend cannot send more in one message (MTCL: 4 GiB); without this cap a
    // larger reply threw in send_file and the server died, leaving the reader waiting.
    const long int nbytes =
        std::min<long int>(remote_reply_size(c_file, offset), backend->max_file_transfer_size());
    const off64_t file_size = c_file.getStoredSize();

    serve_remote_read_request(tid, fd, count, nbytes, file_size, complete, is_getdents, dest,
                              nbytes == 0 ? nullptr : c_file.getBuffer() + offset);
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
    c_file.setRealFileSize(file_size);
    c_file.insertSector(offset, offset + nbytes);
    c_file.setCommitted(complete);

    off64_t end_of_sector = c_file.getSectorEnd(offset);
    c_file.createBuffer(path, false);
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
        // The offset is set before the reply: this runs on the remote listener thread, so once
        // the client has its answer it may close fd before the offset is stored.
        storage_manager->setFileOffset(tid, fd, offset + bytes_read);
        client_manager->replyToClient(tid, offset, c_file.getBuffer(), bytes_read);
    }
}

inline void wait_for_data(const std::filesystem::path &path, const std::string &dest, int tid,
                          int fd, off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, dest=%s, tid=%d, fs=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), dest.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    const CapioFile &c_file = storage_manager->get(path);
    // wait that nbytes are written
    c_file.waitForData(offset + count);
    serve_remote_read(path, dest, tid, fd, count, offset, c_file.isCommitted(), is_getdents);
}

inline void handle_remote_read(const std::filesystem::path &path, const std::string &source,
                               int tid, int fd, off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, source=%s, tid=%d, fd=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), source.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    CapioFile &c_file   = storage_manager->get(path);
    bool data_available = (offset + count <= c_file.getStoredSize());
    if (c_file.isCommitted() || (CapioCLEngine::get().isFirable(path) && data_available)) {
        serve_remote_read(path, source, tid, fd, count, offset, c_file.isCommitted(), is_getdents);
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

    c_file.createBuffer(path, false);
    if (nbytes != 0) {
        auto file_shm_size  = c_file.getBufSize();
        auto file_size_recv = offset + nbytes;
        if (file_size_recv > file_shm_size) {
            c_file.expandBuffer(file_size_recv);
        }
        c_file.readFromNode(source, offset, nbytes);
        nbytes *= sizeof(char);
    }
    handle_read_reply(tid, fd, count, file_size, nbytes, complete, is_getdents);
}

/// "<tid> <fd> <count> <offset> <1 if a getdents> <path>"
inline void remote_read_handler(const RemoteRequest &request) {
    int tid, fd, is_getdents, path_start = 0;
    off64_t count, offset;
    if (sscanf(request.get_content(), "%d %d %ld %ld %d %n", &tid, &fd, &count, &offset,
               &is_getdents, &path_start) < 5 ||
        path_start == 0) {
        return;
    }
    handle_remote_read(request.get_content() + path_start, request.get_source(), tid, fd, count,
                       offset, is_getdents);
}

inline void remote_read_reply_handler(const RemoteRequest &request) {
    const std::string &dest = request.get_source();
    off64_t count, nbytes, file_size;
    int tid, fd, complete, is_getdents;
    sscanf(request.get_content(), "%d %d %ld %ld %ld %d %d", &tid, &fd, &count, &nbytes, &file_size,
           &complete, &is_getdents);
    handle_remote_read_reply(dest, tid, fd, count, nbytes, file_size, complete, is_getdents);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

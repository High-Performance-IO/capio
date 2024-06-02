#ifndef CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_READ_HPP

#include "remote/backend.hpp"
#include "remote/requests.hpp"

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
    CapioFile &c_file          = get_capio_file(path);
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

std::vector<std::string> *files_available(const std::string &prefix, const std::string &app_name,
                                          const std::string &path) {
    START_LOG(gettid(), "call(prefix=%s, app_name=%s, path=%s)", prefix.c_str(), app_name.c_str(),
              path.c_str());

    auto files_to_send                     = new std::vector<std::string>;
    std::unordered_set<std::string> &files = files_sent[app_name];
    auto capio_file_opt                    = get_capio_file_opt(path);

    if (capio_file_opt) {
        if (capio_file_opt->get().is_complete()) {
            files_to_send->emplace_back(path);
            files.insert(path);
        }
    } else {
        return files_to_send;
    }

    for (auto &file_path : get_capio_file_paths()) {
        auto file_location_opt = get_file_location_opt(file_path);

        if (files.find(file_path) == files.end() && file_location_opt &&
            strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 &&
            file_path.native().compare(0, prefix.length(), prefix) == 0) {

            CapioFile &c_file = get_capio_file(file_path);
            if (c_file.is_complete() && !c_file.is_dir()) {
                files_to_send->emplace_back(file_path);
                files.insert(file_path);
            }
        }
    }
    return files_to_send;
}

inline void handle_read_reply(int tid, int fd, long count, off64_t file_size, off64_t nbytes,
                              bool complete, bool is_getdents) {
    START_LOG(
        gettid(),
        "call(tid=%d, fd=%d, count=%ld, file_size=%ld, nbytes=%ld, complete=%s, is_getdents=%s)",
        tid, fd, count, file_size, nbytes, complete ? "true" : "false",
        is_getdents ? "true" : "false");

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t offset                    = get_capio_file_offset(tid, fd);
    c_file.real_file_size             = file_size;
    c_file.insert_sector(offset, offset + nbytes);
    c_file.set_complete(complete);

    off64_t end_of_sector = c_file.get_sector_end(offset);
    c_file.create_buffer_if_needed(false);
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
        send_data_to_client(tid, fd, c_file.get_buffer(), offset, bytes_read);
    }
}

void wait_for_data(const std::filesystem::path &path, const std::string &dest, int tid, int fd,
                   off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, dest=%s, tid=%d, fs=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), dest.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    const CapioFile &c_file = get_capio_file(path);
    // wait that nbytes are written
    c_file.wait_for_data(offset + count);
    serve_remote_read(path, dest, tid, fd, count, offset, c_file.is_complete(), is_getdents);
}

inline void send_files_batch(const std::string &prefix, const std::string &dest, int tid, int fd,
                             off64_t count, bool is_getdents,
                             const std::vector<std::string> *files_to_send) {
    START_LOG(gettid(), "call(prefix=%s, dest=%s, tid=%d, fd=%d, count=%ld, is_getdents=%s)",
              prefix.c_str(), dest.c_str(), tid, fd, count, is_getdents ? "true" : "false");

    // send request
    send_files_batch_request(prefix, tid, fd, count, is_getdents, dest, files_to_send);

    // send data
    for (const std::string &path : *files_to_send) {
        LOG("Sending file %s to target %s", path.c_str(), dest.c_str());
        CapioFile &c_file = get_capio_file(path);
        backend->send_file(c_file.get_buffer(), c_file.get_stored_size(), dest);
    }
}

void wait_for_files_batch(const std::filesystem::path &prefix, const std::string &dest, int tid,
                          int fd, off64_t count, bool is_getdents,
                          const std::vector<std::string> *files, Semaphore *n_files_ready) {
    START_LOG(gettid(), "call(prefix=%s, dest=%s, tid=%d, fd=%d, count=%ld, is_getdents=%s)",
              prefix.c_str(), dest.c_str(), tid, fd, count, is_getdents ? "true" : "false");

    n_files_ready->lock();
    LOG("Files are available. sending batch of files");
    send_files_batch(prefix, dest, tid, fd, count, is_getdents, files);

    delete n_files_ready;
}

inline void handle_remote_read_batch(const std::filesystem::path &path, const std::string &dest,
                                     int tid, int fd, off64_t count, off64_t batch_size,
                                     const std::string &app_name,
                                     const std::filesystem::path &prefix, bool is_getdents) {
    START_LOG(
        gettid(),
        "call(path=%s, dest=%s, tid=%d, fd=%d, count=%ld, batch_size=%ld, app_name=%s, prefix=%s, "
        "is_getdents=%s)",
        path.c_str(), dest.c_str(), tid, fd, count, batch_size, app_name.c_str(), prefix.c_str(),
        is_getdents ? "true" : "false");

    // FIXME: this assignment always overrides the request parameter, which is never used
    batch_size  = find_batch_size(prefix, metadata_conf_globs);
    auto *files = files_available(prefix, app_name, path);
    LOG("files==nullptr? %s", files == nullptr ? "true" : "false");
    if (files->size() == batch_size) {
        LOG("files->size() == batch_size");
        send_files_batch(prefix, dest, tid, fd, count, is_getdents, files);
    } else {
        /*
         * create a thread that waits for the completion of such
         * files and then send those files
         */
        LOG("files->size() != batch_size");
        auto *sem = new Semaphore(0);
        std::thread t(wait_for_files_batch, prefix, dest, tid, fd, count, is_getdents, files, sem);
        t.detach();
        LOG("Thread for batch started.");
        std::lock_guard<std::mutex> lg(nfiles_mutex);
        clients_remote_pending_nfiles[app_name].emplace_back(prefix, batch_size, dest, files, sem);
    }
}

inline void
handle_remote_read_batch_reply(const std::string &source, int tid, int fd, off64_t count,
                               const std::vector<std::pair<std::filesystem::path, off64_t>> &files,
                               bool is_getdents) {
    START_LOG(gettid(), "call(source=%s, tid=%d, fd=%d, count=%ld, is_getdents=%s)", source.c_str(),
              tid, fd, count, is_getdents ? "true" : "false");

    for (const auto &[path, nbytes] : files) {
        auto c_file_opt = get_capio_file_opt(path);
        if (c_file_opt) {
            CapioFile &c_file = c_file_opt->get();
            c_file.create_buffer_if_needed(false);
            size_t file_shm_size = c_file.get_buf_size();
            if (nbytes > file_shm_size) {
                c_file.expand_buffer(nbytes);
            }
            c_file.first_write = false;
        } else {
            add_file_location(path, source.c_str(), -1);
            CapioFile &c_file = create_capio_file(path, false, nbytes);
            c_file.insert_sector(0, nbytes);
            c_file.real_file_size = nbytes;
            c_file.first_write    = false;
            c_file.set_complete();
        }
        // as was done previously, write to the capio file buffer from its beginning
        c_file_opt->get().read_from_node(source, 0, nbytes);
        handle_read_reply(tid, fd, count, nbytes, nbytes, true, is_getdents);
    }
}

inline void handle_remote_read(const std::filesystem::path &path, const std::string &source,
                               int tid, int fd, off64_t count, off64_t offset, bool is_getdents) {
    START_LOG(gettid(),
              "call(path=%s, source=%s, tid=%d, fd=%d, count=%ld, offset=%ld, is_getdents=%s)",
              path.c_str(), source.c_str(), tid, fd, count, offset, is_getdents ? "true" : "false");

    CapioFile &c_file   = get_capio_file(path);
    bool data_available = (offset + count <= c_file.get_stored_size());
    if (c_file.is_complete() ||
        (c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE && data_available)) {
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

    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    off64_t offset                    = get_capio_file_offset(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);

    c_file.create_buffer_if_needed(false);
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

void remote_read_batch_handler(const RemoteRequest &request) {
    const std::string &dest = request.get_source();
    int tid, fd, is_getdents;
    off64_t count, batch_size;
    char path[PATH_MAX], app_name[512], prefix[PATH_MAX];
    sscanf(request.get_content(), "%s %d %d %ld %ld %s %s %d", path, &tid, &fd, &count, &batch_size,
           app_name, prefix, &is_getdents);
    handle_remote_read_batch(path, dest, tid, fd, count, batch_size, app_name, prefix, is_getdents);
}

// TODO: refactor this
void remote_read_batch_reply_handler(const RemoteRequest &request) {
    std::string dest = request.get_source();
    std::string path, prefix, tmp;
    std::vector<std::pair<std::filesystem::path, off64_t>> files;

    std::istringstream content(request.get_content());
    std::getline(content, prefix, ' ');
    std::getline(content, tmp, ' ');
    int tid = std::stoi(tmp);
    std::getline(content, tmp, ' ');
    int fd = std::stoi(tmp);
    std::getline(content, tmp, ' ');
    off64_t count = std::stol(tmp);
    std::getline(content, tmp, ' ');
    bool is_getdents = std::stoi(tmp);

    while (getline(content, path, ' ')) {
        path = prefix.append(path);
        std::getline(content, tmp, ' ');
        files.emplace_back(path, std::stol(tmp));
    }

    handle_remote_read_batch_reply(dest, tid, fd, count, files, is_getdents);
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

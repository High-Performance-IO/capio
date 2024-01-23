#ifndef CAPIO_SERVER_REMOTE_HANDLERS_SEND_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_SEND_HPP

#include "remote/backend.hpp"

inline void handle_remote_send(const std::filesystem::path &path, off64_t offset, off64_t nbytes,
                               bool complete, off64_t file_size, int dest) {
    START_LOG(gettid(),
              "call(path=%s, offset=%ld, nbytes=%ld, complete=%s, file_size=%ld, dest=%d)",
              path.c_str(), offset, nbytes, complete ? "true" : "false", file_size, dest);

    char *file_shm    = nullptr;
    CapioFile &c_file = get_capio_file(path);
    c_file.create_buffer_if_needed(path, false);
    if (nbytes != 0) {
        auto file_shm_size  = c_file.get_buf_size();
        auto file_size_recv = offset + nbytes;
        if (file_size_recv > file_shm_size) {
            file_shm = c_file.expand_buffer(file_size_recv);
        } else {
            file_shm = c_file.get_buffer();
        }
        backend->recv_file(file_shm + offset, dest, nbytes);
        nbytes *= sizeof(char);
    }
    read_reply_request(path, file_size, offset, nbytes, complete);
}

inline void
handle_remote_send_batch(const std::vector<std::pair<std::filesystem::path, off64_t>> &files,
                         int dest) {
    for (const auto &[path, nbytes] : files) {
        void *p_shm;
        auto c_file_opt = get_capio_file_opt(path);
        if (c_file_opt) {
            CapioFile &c_file = get_capio_file(path);
            c_file.create_buffer_if_needed(path, false);
            p_shm                = c_file.get_buffer();
            size_t file_shm_size = c_file.get_buf_size();
            if (nbytes > file_shm_size) {
                p_shm = c_file.expand_buffer(nbytes);
            }
            c_file.first_write = false;
        } else {
            auto node_name_src = rank_to_node[dest];
            add_file_location(path, node_name_src.c_str(), -1);
            p_shm             = new char[nbytes];
            CapioFile &c_file = create_capio_file(path, false, nbytes);
            c_file.insert_sector(0, nbytes);
            c_file.real_file_size = nbytes;
            c_file.first_write    = false;
            c_file.set_complete();
        }
        backend->recv_file((char *) p_shm, dest, nbytes);
        read_reply_request(path, nbytes, 0, nbytes, true);
    }
}

void remote_send_batch_handler(const RemoteRequest &request) {
    int dest = request.get_source();
    std::string path, prefix, nbytes;
    std::istringstream content(request.get_content());
    std::vector<std::pair<std::filesystem::path, off64_t>> files;

    std::getline(content, path, ' ');
    std::getline(content, prefix, ' ');

    while (getline(content, path, ' ')) {
        path = prefix.append(path);
        std::getline(content, nbytes, ' ');
        files.emplace_back(path, std::stol(nbytes));
    }
    handle_remote_send_batch(files, dest);
}

void remote_send_handler(const RemoteRequest &request) {
    int dest = request.get_source();
    off64_t offset, nbytes, file_size;
    char path[PATH_MAX];
    int complete;
    sscanf(request.get_content(), "%s %ld %ld %d %ld", path, &offset, &nbytes, &complete,
           &file_size);
    handle_remote_send(path, offset, nbytes, complete, file_size, dest);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_SEND_HPP

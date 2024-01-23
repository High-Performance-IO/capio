#ifndef CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

#include "remote/backend.hpp"

void wait_for_completion(const std::filesystem::path &path, const CapioFile &c_file, int dest,
                         sem_t *data_is_complete) {
    START_LOG(gettid(), "call(path=%s, dest=%d)", path.c_str(), dest);

    SEM_WAIT_CHECK(data_is_complete, "data_is_complete");
    LOG("File %s has been completed. serving stats data", path.c_str());
    backend->serve_remote_stat(path, dest, c_file);

    delete data_is_complete;
}

void wait_for_file_stat(const std::filesystem::path &path, int dest, CapioFile &c_file) {
    START_LOG(gettid(), "call(path_c=%s, dest=%d, c_file=%ld)", path.c_str(), dest, &c_file);
    auto sem = new sem_t;

    if (sem_init(sem, 0, 0) == -1) {
        ERR_EXIT("sem_init in remote_listener_stat_req");
    }
    clients_remote_pending_stat[path].emplace_back(sem);
    std::thread t(wait_for_completion, path, std::cref(c_file), dest, sem);
    t.detach();
}

inline void handle_remote_stat(const std::filesystem::path &path, int dest) {
    START_LOG(gettid(), "call(path=%s, dest=%d)", path.c_str(), dest);

    auto c_file = get_capio_file_opt(path);
    if (c_file) {
        if (c_file->get().is_complete()) {
            LOG("file is complete. serving file");
            backend->serve_remote_stat(path, dest, c_file->get());
        } else { // wait for completion
            LOG("File is not complete. awaiting completion on different thread");
            wait_for_file_stat(path, dest, c_file->get());
        }
    } else {
        LOG("CAPIO file is not in metadata. checking in globs for files to be created");
        if (match_globs(path) != -1) {
            LOG("File is in globs. creating capio_file and starting thread awaiting for future "
                "creation of file");
            CapioFile &file = create_capio_file(path, false, CAPIO_DEFAULT_FILE_INITIAL_SIZE);
            wait_for_file_stat(path, dest, file);
        } else {
            ERR_EXIT("Error capio file is not present, nor is going to be created in the future.");
        }
    }
}

void remote_stat_handler(const RemoteRequest &request) {
    char path[PATH_MAX];
    int dest;
    sscanf(request.get_content(), "%d %s", &dest, path);
    handle_remote_stat(path, dest);
}

void remote_stat_reply_handler(const RemoteRequest &request) {
    char path[PATH_MAX];
    off64_t size;
    int dir;
    sscanf(request.get_content(), "%s %ld %d", path, &size, &dir);
    stat_reply_request(path, size, dir);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

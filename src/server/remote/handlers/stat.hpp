#ifndef CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

#include "remote/backend.hpp"

void wait_for_completion(const std::filesystem::path &path, int source_tid, int dest) {
    START_LOG(gettid(), "call(path=%s, dest=%d)", path.c_str(), dest);

    const CapioFile &c_file = get_capio_file(path);
    c_file.wait_for_completion();
    LOG("File %s has been completed. serving stats data", path.c_str());
    backend->serve_remote_stat(path, dest, source_tid);
}

inline void handle_remote_stat(int source_tid, const std::filesystem::path &path, int dest) {
    START_LOG(gettid(), "call(source_tid=%d, path=%s, dest=%d)", source_tid, path.c_str(), dest);

    auto c_file = get_capio_file_opt(path);
    if (c_file) {
        if (c_file->get().is_complete() || c_file->get().get_mode() == CAPIO_FILE_MODE_NO_UPDATE) {
            LOG("file is complete. serving file");
            backend->serve_remote_stat(path, dest, source_tid);
        } else { // wait for completion
            LOG("File is not _complete. awaiting completion on different thread");
            std::thread t(wait_for_completion, path, source_tid, dest);
            t.detach();
        }
    } else {
        LOG("CAPIO file is not in metadata. checking in globs for files to be created");
        if (match_globs(path) != -1) {
            LOG("File is in globs. creating capio_file and starting thread awaiting for future "
                "creation of file");
            create_capio_file(path, false, CAPIO_DEFAULT_FILE_INITIAL_SIZE);
            std::thread t(wait_for_completion, path, source_tid, dest);
            t.detach();
        } else {
            ERR_EXIT("Error capio file is not present, nor is going to be created in the future.");
        }
    }
}

inline void handle_remote_stat_reply(const std::filesystem::path &path, int source_tid,
                                     off64_t size, bool dir) {
    START_LOG(gettid(), "call(path=%s, source_tid=%d, size=%ld, dir=%s)", path.c_str(), source_tid,
              size, dir ? "true" : "false");

    write_response(source_tid, size);
    write_response(source_tid, static_cast<off64_t>(dir));
}

void remote_stat_handler(const RemoteRequest &request) {
    char path[PATH_MAX];
    int dest, tid;
    sscanf(request.get_content(), "%d %d %s", &tid, &dest, path);
    handle_remote_stat(tid, path, dest);
}

void remote_stat_reply_handler(const RemoteRequest &request) {
    char path[PATH_MAX];
    off64_t size;
    int dir, source_tid;
    sscanf(request.get_content(), "%s %d %ld %d", path, &source_tid, &size, &dir);
    handle_remote_stat_reply(path, source_tid, size, dir);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

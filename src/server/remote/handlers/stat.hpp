#ifndef CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_STAT_HPP

#include "remote/backend.hpp"
#include "remote/requests.hpp"

inline void serve_remote_stat(const std::filesystem::path &path, const std::string &dest,
                              int source_tid) {
    START_LOG(gettid(), "call(path=%s, dest=%s, source_tid%d)", path.c_str(), dest.c_str(),
              source_tid);

    const CapioFile &c_file = get_capio_file(path);
    off64_t file_size       = c_file.get_file_size();
    bool is_dir             = c_file.is_dir();
    serve_remote_stat_request(path, source_tid, file_size, is_dir, dest);
}

void wait_for_completion(const std::filesystem::path &path, int source_tid,
                         const std::string &dest) {
    START_LOG(gettid(), "call(path=%s, source_tid=%d, dest=%s)", path.c_str(), source_tid,
              dest.c_str());

    const CapioFile &c_file = get_capio_file(path);
    c_file.wait_for_completion();
    LOG("File %s has been completed. serving stats data", path.c_str());
    serve_remote_stat(path, dest, source_tid);
}

inline void handle_remote_stat(int source_tid, const std::filesystem::path &path,
                               const std::string &dest) {
    START_LOG(gettid(), "call(source_tid=%d, path=%s, dest=%s)", source_tid, path.c_str(),
              dest.c_str());

    auto c_file = get_capio_file_opt(path);
    if (c_file) {
        LOG("File %s is present on capio file system", path.c_str());
        if (c_file->get().is_complete() || c_file->get().firing_rule() == CAPIO_FILE_MODE_NO_UPDATE) {
            LOG("file is complete. serving file");
            serve_remote_stat(path, dest, source_tid);
        } else { // wait for completion
            LOG("File is not complete. awaiting completion on different thread. parameters of wait "
                "are: path=%s, source_tid=%d, dest=%s",
                path.c_str(), source_tid, dest.c_str());
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
    char path[PATH_MAX], dest[HOST_NAME_MAX];
    int tid;
    sscanf(request.get_content(), "%d %s %s", &tid, dest, path);
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

#ifndef CAPIO_SERVER_HANDLERS_STAT_HPP
#define CAPIO_SERVER_HANDLERS_STAT_HPP

#include <mutex>
#include <thread>

#include "utils/location.hpp"
#include "utils/producer.hpp"
#include "utils/types.hpp"

CSMyRemotePendingStats_t pending_remote_stats;
std::mutex pending_remote_stats_mutex;

// TODO: replace the direct call with dome king of intra thread communication to avoid passing
// values from one to another
#include "../comm/remote_listener.hpp"

inline void reply_stat(int tid, const std::string &path, int rank) {
    START_LOG(gettid(), "call(tid=%d, path=%s, rank=%d)", tid, path.c_str(), rank);
    auto file_location_opt = get_file_location_opt(path.c_str());

    auto file_is_remote = check_file_location(rank, path);
    LOG("File %s is local? %s", path.c_str(), file_is_remote ? "True" : "False");

    if (!file_is_remote && !file_location_opt) {
        LOG("get_file_location_opt returned an empty object and check_files_location failed!");
        // if it is in configuration file then wait otherwise fails
        if ((metadata_conf.find(path) != metadata_conf.end() || match_globs(path) != -1) &&
            !is_producer(tid, path)) {
            LOG("File not ready yet. Starting a thread to wait for file.");
            std::thread t(wait_for_stat, tid, std::string(path), rank, &pending_remote_stats,
                          &pending_remote_stats_mutex);
            t.detach();
        } else {
            LOG("Metadata do not contains file or globs did not contain file or app is producer.");
            write_response(tid, -1); // return size
            write_response(tid, -1); // return is_dir
        }
        return;
    }
    auto c_file_opt = get_capio_file_opt(path.c_str());
    Capio_file &c_file =
        (c_file_opt) ? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    LOG("Obtained capio file. ready to reply to client");
    const std::filesystem::path &capio_dir = get_capio_dir();
    LOG("Obtained capio_dir");
    if (!file_location_opt) {
        LOG("File is now present from remote node. retrieving file again.");
        file_location_opt = get_file_location_opt(path.c_str());
    }
    if (c_file.is_complete() || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 ||
        c_file.get_mode() == CAPIO_FILE_MODE_NO_UPDATE || capio_dir == path) {
        LOG("Sending response to client");
        write_response(tid, c_file.get_file_size());
        write_response(tid, static_cast<int>(c_file.is_dir() ? 1 : 0));
    } else {
        LOG("Delegating backend to reply to remote stats");
        backend->handle_remote_stat(tid, path, rank, &pending_remote_stats,
                                    &pending_remote_stats_mutex);
    }
}

inline void handle_stat_reply(const char *path_c, off64_t size, int dir) {
    START_LOG(gettid(), "call(path_c=%s, size=%ld, dir=%d)", path_c, size, dir);

    const std::lock_guard<std::mutex> lg(pending_remote_stats_mutex);
    auto it = pending_remote_stats.find(path_c);
    if (it == pending_remote_stats.end()) {
        LOG("handle_stat_reply %s not found, stat already answered for "
            "optimization",
            path_c);
    } else {
        for (int tid : it->second) {
            write_response(tid, size);
            write_response(tid, static_cast<off64_t>(dir));
        }
        pending_remote_stats.erase(it);
    }
}

void fstat_handler(const char *const str, int rank) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    reply_stat(tid, get_capio_file_path(tid, fd).data(), rank);
}

void stat_handler(const char *const str, int rank) {
    char path[2048];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    reply_stat(tid, path, rank);
}

void stat_reply_handler(const char *const str, int rank) {
    char path_c[1024];
    off64_t size;
    int dir;
    sscanf(str, "%s %ld %d", path_c, &size, &dir);
    handle_stat_reply(path_c, size, dir);
}

#endif // CAPIO_SERVER_HANDLERS_STAT_HPP

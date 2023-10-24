#ifndef CAPIO_SERVER_HANDLERS_STAT_HPP
#define CAPIO_SERVER_HANDLERS_STAT_HPP

#include <mutex>
#include <thread>

#include "utils/location.hpp"
#include "utils/types.hpp"

CSMyRemotePendingStats_t pending_remote_stats;
std::mutex pending_remote_stats_mutex;

inline void handle_local_stat(int tid, const std::string& path) {
    START_LOG(tid, "call(tid=%d, path=%s)", tid, path.c_str());

    Capio_file &c_file = get_capio_file(path.c_str());
    write_response(tid, c_file.get_file_size());
    write_response(tid, static_cast<int>(c_file.is_dir()? 0 : 1));
}

inline void handle_remote_stat(int tid, const std::string& path, int rank) {
    START_LOG(tid, "call(tid=%d, path=%s, rank=%d)", tid, path.c_str(), rank);

    const std::lock_guard<std::mutex> lg(pending_remote_stats_mutex);
    std::string str_msg;
    int dest = nodes_helper_rank[std::get<0>(get_file_location(path.c_str()))];
    str_msg = "stat " + std::to_string(rank) + " " + path;
    const char *msg = str_msg.c_str();
    MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    pending_remote_stats[path].emplace_back(tid);
}

void wait_for_stat(int tid, const std::string& path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    const std::string& path_to_check(path);
    loop_check_files_location(path_to_check, rank);
    //check if the file is local or remote
    Capio_file &c_file = get_capio_file(path.c_str());
    std::string_view mode = c_file.get_mode();
    bool complete = c_file.complete;
    if (complete || strcmp(std::get<0>(get_file_location(path_to_check.c_str())), node_name) == 0 || mode == CAPIO_FILE_MODE_NOUPDATE) {
        handle_local_stat(tid, path);
    } else {
        handle_remote_stat(tid, path, rank);
    }
}


inline void reply_stat(int tid, const std::string &path, int rank) {
    START_LOG(gettid(), "call(tid=%d, path=%s, rank=%d)", tid, path.c_str(), rank);

    auto file_location_opt = get_file_location_opt(path.c_str());
    if (!file_location_opt) {
        check_file_location(rank, path);
        //if it is in configuration file then wait otherwise fails
        if ((metadata_conf.find(path) != metadata_conf.end() || match_globs(path) != -1) &&
            !is_producer(tid, path)) {
            std::thread t(wait_for_stat, tid, std::string(path));
            t.detach();
        } else {
            write_response(tid, -1);
        }
        return;
    }
    auto c_file_opt = get_capio_file_opt(path.c_str());
    Capio_file &c_file = (c_file_opt)? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    std::string_view mode = c_file.get_mode();
    bool complete = c_file.complete;
    const std::string *capio_dir = get_capio_dir();
    if (complete || strcmp(std::get<0>(file_location_opt->get()), node_name) == 0 || mode == CAPIO_FILE_MODE_NOUPDATE ||
        *capio_dir == path) {
        handle_local_stat(tid, path);
    } else {
        handle_remote_stat(tid, path, rank);
    }
}

inline void handle_stat_reply(const char *path_c, off64_t size, int dir) {
    START_LOG(gettid(), "call(path_c=%s, size=%ld, dir=%d)", path_c, size, dir);

    const std::lock_guard<std::mutex> lg(pending_remote_stats_mutex);
    auto it = pending_remote_stats.find(path_c);
    if (it == pending_remote_stats.end()) {
        LOG("handle_stat_reply %s not found, stat already answered for optimization", path_c);
    } else {
        for (int tid: it->second) {
            write_response(tid, size);
            write_response(tid, static_cast<off64_t>(dir));
        }
        pending_remote_stats.erase(it);
    }
}

void fstat_handler(const char * const str, int rank) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    reply_stat(tid, get_capio_file_path(tid, fd).data(), rank);
}

void stat_handler(const char * const str, int rank) {
    char path[2048];
    int tid;
    sscanf(str, "%d %s", &tid, path);
    reply_stat(tid, path, rank);
}

void stat_reply_handler(const char * const str, int rank) {
    char path_c[1024];
    off64_t size;
    int dir;
    sscanf(str, "%s %ld %d", path_c, &size, &dir);
    handle_stat_reply(path_c, size, dir);
}

#endif // CAPIO_SERVER_HANDLERS_STAT_HPP

#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include "utils/filesystem.hpp"
#include "utils/location.hpp"
#include "utils/metadata.hpp"

inline void update_file_metadata(const std::string &path, int tid, int fd, int rank,
                                 bool is_creat) {
    START_LOG(tid, "call(path=%s, client_tid=%d fd=%d, rank=%d, is_creat=%s)", path.c_str(), tid,
              fd, rank, is_creat ? "true" : "false");

    // TODO: check the size that the user wrote in the configuration file
    //*caching_info[tid].second += 2;
    auto c_file_opt = get_capio_file_opt(path.c_str());
    Capio_file &c_file =
        (c_file_opt) ? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    add_capio_file_to_tid(tid, fd, path);
    int pid       = pids[tid];
    auto it_files = writers.find(pid);
    if (it_files != writers.end()) {
        if (it_files->second.find(path) == it_files->second.end()) {
            LOG("setting writers[%ld][%s]=false 1", pid, path.c_str());
            writers[pid][path] = false;
        }
    } else {
        LOG("setting writers[%ld][%s]=true", pid, path.c_str());
        writers[pid][path] = true;
    }
    if (c_file.first_write && is_creat) {
        c_file.first_write = false;
        write_file_location(rank, path, tid);
        update_dir(tid, path, rank);
    }
}

inline void handle_create(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)", tid, fd, path_cstr, rank);

    bool is_creat = !(get_file_location_opt(path_cstr) || check_file_location(rank, path_cstr));
    update_file_metadata(path_cstr, tid, fd, rank, is_creat);
    write_response(tid, 0);
}

inline void handle_create_exclusive(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)", tid, fd, path_cstr, rank);

    if (get_capio_file_opt(path_cstr)) {
        write_response(tid, 1);
    } else {
        write_response(tid, 0);
        update_file_metadata(path_cstr, tid, fd, rank, true);
    }
}

inline void handle_open(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)", tid, fd, path_cstr, rank);

    // it is important that check_files_location is the last because is the
    // slowest (short circuit evaluation)
    if (get_file_location_opt(path_cstr) || metadata_conf.find(path_cstr) != metadata_conf.end() ||
        match_globs(path_cstr) != -1 || check_file_location(rank, path_cstr)) {
        update_file_metadata(path_cstr, tid, fd, rank, false);
    } else {
        write_response(tid, 1);
    }
    write_response(tid, 0);
}

void create_handler(const char *const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_create(tid, fd, path_cstr, rank);
}

void create_exclusive_handler(const char *const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_create_exclusive(tid, fd, path_cstr, rank);
}

void open_handler(const char *const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_open(tid, fd, path_cstr, rank);
}

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

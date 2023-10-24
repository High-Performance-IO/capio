#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

#include "utils/location.hpp"
#include "utils/metadata.hpp"

inline void update_file_metadata(const std::string& path, int tid, int fd, int rank, bool is_creat) {
    START_LOG(tid, "call(path=%s, fd=%d, rank=%d, is_creat=%s)", path.c_str(), fd, rank, is_creat? "true" : "false");

    //TODO: check the size that the user wrote in the configuration file
    off64_t *p_offset = (off64_t *) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd),
                                               sizeof(off64_t));
    //*caching_info[tid].second += 2;
    auto c_file_opt = get_capio_file_opt(path.c_str());
    Capio_file &c_file = (c_file_opt)? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    c_file.open();
    add_capio_file_to_tid(tid, fd, path);
    processes_files[tid][fd] = std::make_tuple(&c_file, p_offset);//TODO: what happens if a process open the same file twice?
    int pid = pids[tid];
    auto it_files = writers.find(pid);
    if (it_files != writers.end()) {
        if (it_files->second.find(path) == it_files->second.end()) {
            writers[pid][path] = false;
        }
    } else {
        writers[pid][path] = false;
    }
    if (c_file.first_write && is_creat) {
        c_file.first_write = false;
        write_file_location(rank, path, tid);
        update_dir(tid, path, rank);
    }
}


inline void handle_create(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)",
              tid, fd, path_cstr, rank);

    bool is_creat = !(get_file_location_opt(path_cstr) || check_file_location(rank, path_cstr));
    update_file_metadata(path_cstr, tid, fd, rank, is_creat);
    write_response(tid, 0);
}

inline void handle_create_exclusive(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)",
              tid, fd, path_cstr, rank);

    if (get_capio_file_opt(path_cstr)) {
        write_response(tid, 1);
    } else {
        write_response(tid, 0);
        update_file_metadata(path_cstr, tid, fd, rank, true);
    }
}

inline void handle_open(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)",
              tid, fd, path_cstr, rank);

    //it is important that check_files_location is the last beacuse is the slowest (short circuit evalutation)
    if (get_file_location_opt(path_cstr) || metadata_conf.find(path_cstr) != metadata_conf.end() || match_globs(path_cstr) != -1 || check_file_location(rank, path_cstr)) {
        update_file_metadata(path_cstr, tid, fd, rank, false);
    } else {
        write_response(tid, 1);
    }
    write_response(tid, 0);
}

void create_handler(const char * const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_create(tid, fd, path_cstr, rank);
}

void create_exclusive_handler(const char * const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_create_exclusive(tid, fd, path_cstr, rank);
}

void open_handler(const char * const str, int rank) {
    int tid, fd;
    char path_cstr[PATH_MAX];
    sscanf(str, "%d %d %s", &tid, &fd, path_cstr);
    handle_open(tid, fd, path_cstr, rank);
}

#endif // CAPIO_SERVER_HANDLERS_OPEN_HPP

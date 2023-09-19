#ifndef CAPIO_SERVER_HANDLERS_OPEN_HPP
#define CAPIO_SERVER_HANDLERS_OPEN_HPP

inline void handle_create(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)",
              tid, fd, path_cstr, rank);

    init_process(tid);
    bool is_creat = !(files_location.find(path_cstr) != files_location.end() || check_file_location(rank, path_cstr));
    update_file_metadata(path_cstr, tid, fd, rank, is_creat);
    write_response(tid, 0);
}

inline void handle_create_exclusive(int tid, int fd, const char *path_cstr, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path_cstr=%s, rank=%d)",
              tid, fd, path_cstr, rank);

    init_process(tid);
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

    init_process(tid);
    //it is important that check_files_location is the last beacuse is the slowest (short circuit evalutation)
    if (files_location.find(path_cstr) != files_location.end() || metadata_conf.find(path_cstr) != metadata_conf.end() || match_globs(path_cstr, &metadata_conf_globs) != -1 || check_file_location(rank, path_cstr)) {
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

#ifndef CAPIO_SERVER_HANDLERS_SEEK_HPP
#define CAPIO_SERVER_HANDLERS_SEEK_HPP

#include "stat.hpp"

inline void handle_lseek(int tid, int fd, size_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    Capio_file &c_file = get_capio_file(get_capio_file_path(tid, fd).data());
    write_response(tid, c_file.get_sector_end(offset));
}

void handle_seek_data(int tid, int fd, size_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    Capio_file &c_file = get_capio_file(get_capio_file_path(tid, fd).data());
    write_response(tid, c_file.seek_data(offset));
}

inline void handle_seek_end(int tid, int fd, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, rank=%d)", tid, fd, rank);

    // seek_end here behaves as stat because we want the file size
    reply_stat(tid, get_capio_file_path(tid, fd).data(), rank);
}

inline void handle_seek_hole(int tid, int fd, size_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    Capio_file &c_file = get_capio_file(get_capio_file_path(tid, fd).data());
    write_response(tid, c_file.seek_hole(offset));
}

void lseek_handler(const char *const str, int rank) {
    int tid, fd;
    size_t offset;
    sscanf(str, "%d %d %zu", &tid, &fd, &offset);
    handle_lseek(tid, fd, offset);
}

void seek_data_handler(const char *const str, int rank) {
    int tid, fd;
    size_t offset;
    sscanf(str, "%d %d %zu", &tid, &fd, &offset);
    handle_seek_data(tid, fd, offset);
}

void seek_end_handler(const char *const str, int rank) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_seek_end(tid, fd, rank);
}

void seek_hole_handler(const char *const str, int rank) {
    int tid, fd;
    size_t offset;
    sscanf(str, "%d %d %zu", &tid, &fd, &offset);
    handle_seek_hole(tid, fd, offset);
}

#endif // CAPIO_SERVER_HANDLERS_SEEK_HPP

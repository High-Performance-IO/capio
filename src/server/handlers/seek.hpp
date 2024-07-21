#ifndef CAPIO_SERVER_HANDLERS_SEEK_HPP
#define CAPIO_SERVER_HANDLERS_SEEK_HPP

#include "stat.hpp"

inline void handle_lseek(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    set_capio_file_offset(tid, fd, offset);
    write_response(tid, offset);
}

void handle_seek_data(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = get_capio_file(get_capio_file_path(tid, fd));
    offset            = c_file.seek(CapioFile::seek_type::data, offset);
    set_capio_file_offset(tid, fd, offset);
    write_response(tid, offset);
}

inline void handle_seek_end(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    // seek_end here behaves as stat because we want the file size
    reply_stat(tid, get_capio_file_path(tid, fd));
}

inline void handle_seek_hole(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = get_capio_file(get_capio_file_path(tid, fd));
    offset            = c_file.seek(CapioFile::seek_type::hole, offset);
    set_capio_file_offset(tid, fd, offset);
    write_response(tid, offset);
}

void lseek_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_lseek(tid, fd, offset);
}

void seek_data_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_data(tid, fd, offset);
}

void seek_end_handler(const char *const str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_seek_end(tid, fd);
}

void seek_hole_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_hole(tid, fd, offset);
}

#endif // CAPIO_SERVER_HANDLERS_SEEK_HPP

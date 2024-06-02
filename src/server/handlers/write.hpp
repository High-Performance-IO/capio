#ifndef CAPIO_SERVER_HANDLERS_WRITE_HPP
#define CAPIO_SERVER_HANDLERS_WRITE_HPP

#include "utils/location.hpp"
#include "utils/metadata.hpp"

void write_handler(const char *const str) {
    std::string request;
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);

    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);
    // check if another process is waiting for this data
    off64_t offset                    = get_capio_file_offset(tid, fd);
    off64_t end_of_write              = offset + count;
    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    off64_t file_shm_size             = c_file.get_buf_size();
    auto *data_buf                    = data_buffers[tid].first;

    c_file.create_buffer_if_needed(false);
    if (end_of_write > file_shm_size) {
        c_file.expand_buffer(end_of_write);
    }
    c_file.read_from_queue(*data_buf, offset, count);

    int pid            = pids[tid];
    writers[pid][path] = true;
    c_file.insert_sector(offset, end_of_write);
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(path);
        // TODO: it works only if there is one prod per file
        update_dir(tid, path);
    }
    set_capio_file_offset(tid, fd, end_of_write);
}

#endif // CAPIO_SERVER_HANDLERS_WRITE_HPP

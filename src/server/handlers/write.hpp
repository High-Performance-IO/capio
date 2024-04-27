#ifndef CAPIO_SERVER_HANDLERS_WRITE_HPP
#define CAPIO_SERVER_HANDLERS_WRITE_HPP

#include "utils/location.hpp"
#include "utils/metadata.hpp"

inline void handle_write(int tid, int fd, off64_t base_offset, off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, base_offset=%ld, count=%ld)", tid, fd, base_offset,
              count);
    // check if another process is waiting for this data
    off64_t end_of_read               = base_offset + count;
    const std::filesystem::path &path = get_capio_file_path(tid, fd);
    CapioFile &c_file                 = get_capio_file(path);
    size_t file_shm_size              = c_file.get_buf_size();
    auto *data_buf                    = data_buffers[tid].first;
    off64_t n_reads                   = count / CAPIO_DATA_BUFFER_ELEMENT_SIZE;
    off64_t r                         = count % CAPIO_DATA_BUFFER_ELEMENT_SIZE;

    c_file.create_buffer_if_needed(path, true);
    if (end_of_read > file_shm_size) {
        c_file.expand_buffer(end_of_read);
    }
    for (int i = 0; i < n_reads; i++) {
        c_file.read_from_queue(*data_buf, base_offset + i * CAPIO_DATA_BUFFER_ELEMENT_SIZE);
    }
    if (r) {
        c_file.read_from_queue(*data_buf, base_offset + n_reads * CAPIO_DATA_BUFFER_ELEMENT_SIZE,
                               r);
    }
    int pid            = pids[tid];
    writers[pid][path] = true;
    c_file.insert_sector(base_offset, end_of_read);
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(path);
        // TODO: it works only if there is one prod per file
        update_dir(tid, path);
    }
}

void write_handler(const char *const str) {
    std::string request;
    int tid, fd;
    off64_t base_offset, count;
    sscanf(str, "%d %d %ld %ld", &tid, &fd, &base_offset, &count);
    handle_write(tid, fd, base_offset, count);
}

#endif // CAPIO_SERVER_HANDLERS_WRITE_HPP

#ifndef CAPIO_SERVER_HANDLERS_WRITE_HPP
#define CAPIO_SERVER_HANDLERS_WRITE_HPP

#include "utils/location.hpp"
#include "utils/metadata.hpp"



inline void handle_write(int tid, int fd, off64_t base_offset, off64_t count, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, base_offset=%ld, count=%ld, rank=%d)", tid, fd, base_offset, count, rank);
    //check if another process is waiting for this data
    off64_t data_size = base_offset + count;
    std::string_view path = get_capio_file_path(tid, fd);
    Capio_file &c_file = init_capio_file(path.data(), true);
    size_t file_shm_size = c_file.get_buf_size();
    auto *data_buf = data_buffers[tid].first;
    size_t n_reads = count / WINDOW_DATA_BUFS;
    size_t r = count % WINDOW_DATA_BUFS;
    size_t i = 0;
    char *p;
    if (data_size > file_shm_size)
        p = expand_memory_for_file(path.data(), data_size, c_file);
    p = c_file.get_buffer();
    p = p + base_offset;
    while (i < n_reads) {
        data_buf->read(p + i * WINDOW_DATA_BUFS);
        ++i;
    }
    if (r)
        data_buf->read(p + i * WINDOW_DATA_BUFS, r);
    int pid = pids[tid];
    writers[pid][path.data()] = true;
    c_file.insert_sector(base_offset, data_size);
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(rank, path.data(), tid);
        //TODO: it works only if there is one prod per file
        update_dir(tid, path.data(), rank);
    }
    std::string_view mode = c_file.get_mode();
    auto it = pending_reads.find(path.data());
    if (it != pending_reads.end() && mode == CAPIO_FILE_MODE_NOUPDATE) {
        auto &pending_reads_this_file = it->second;
        auto it_vec = pending_reads_this_file.begin();
        while (it_vec != pending_reads_this_file.end()) {
            auto tuple = *it_vec;
            int pending_tid = std::get<0>(tuple);
            int fd = std::get<1>(tuple);
            size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
            size_t count = std::get<2>(tuple);
            size_t file_size = c_file.get_stored_size();
            if (process_offset + count <= file_size) {
                handle_pending_read(pending_tid, fd, process_offset, count, std::get<3>(tuple));
                it_vec = pending_reads_this_file.erase(it_vec);
            } else
                ++it_vec;
        }
    }
    if (mode == CAPIO_FILE_MODE_NOUPDATE)
        handle_pending_remote_reads(path.data(), data_size, false);
}

void write_handler(const char * const str, int rank) {
    std::string request;
    int tid, fd;
    off64_t base_offset, count;
    sscanf(str, "%d %d %ld %ld", &tid, &fd, &base_offset, &count);
    handle_write(tid, fd, base_offset, count, rank);
}

#endif // CAPIO_SERVER_HANDLERS_WRITE_HPP

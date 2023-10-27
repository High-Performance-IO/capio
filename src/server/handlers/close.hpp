#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "read.hpp"

inline void handle_close(int tid, int fd, int rank) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, rank=%d)", tid, fd, rank);

    std::string_view path = get_capio_file_path(tid, fd);
    if (path.empty()) { // avoid to try to close a file that does not exists
                        // (example: try to close() on a dir
        return;
    }

    Capio_file &c_file = get_capio_file(path.data());
    c_file.close();
    if (c_file.get_committed() == "on_close" && c_file.is_closed()) {
        c_file.complete = true;
        auto it = pending_reads.find(path.data());
        if (it != pending_reads.end()) {
            auto &pending_reads_this_file = it->second;
            auto it_vec = pending_reads_this_file.begin();
            while (it_vec != pending_reads_this_file.end()) {
                auto tuple = *it_vec;
                int pending_tid = std::get<0>(tuple);
                int fd = std::get<1>(tuple);
                size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                size_t count = std::get<2>(tuple);

                bool is_getdents = std::get<3>(tuple);
                handle_pending_read(pending_tid, fd, process_offset, count, is_getdents);
                it_vec = pending_reads_this_file.erase(it_vec);
            }
            pending_reads.erase(it);
        }
        if (c_file.is_dir()) {
            reply_remote_stats(path.data());
        }
        // TODO: error if seek are done and also do this on exit
        handle_pending_remote_reads(path.data(), c_file.get_sector_end(0), true);
        handle_pending_remote_nfiles(path.data());
        c_file.commit();
    }

    if (c_file.is_deletable()) {
        delete_capio_file(path.data());
        delete_from_file_locations(path.data(), rank);
    }
    std::string offset_name = "offset_" + std::to_string(tid) + "_" + std::to_string(fd);
    if (shm_unlink(offset_name.c_str()) == -1) {
        ERR_EXIT("shm_unlink %s in handle_close", offset_name.c_str());
    }
    processes_files[tid].erase(fd);
    delete_capio_file_from_tid(tid, fd);
}

void close_handler(const char *str, int rank) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_close(tid, fd, rank);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP

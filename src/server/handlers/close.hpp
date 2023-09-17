#ifndef CAPIO_SERVER_HANDLERS_CLOSE_HPP
#define CAPIO_SERVER_HANDLERS_CLOSE_HPP

#include "read.hpp"

void handle_close(int tid, int fd, int rank) {
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    ++c_file._n_close;
    if (c_file.get_committed() == "on_close" && (c_file._n_close_expected == -1 || c_file._n_close == c_file._n_close_expected)) {
#ifdef CAPIOLOG
        logfile <<  "handle close, committed = on_close. n_close " << c_file._n_close << " n_close_expected " << c_file._n_close_expected << std::endl;
#endif
        c_file.complete = true;
        auto it = pending_reads.find(path);
        if (it != pending_reads.end()) {
#ifdef CAPIOLOG
            logfile << "handle pending read file on_close " << path << std::endl;
#endif
            auto& pending_reads_this_file = it->second;
            auto it_vec = pending_reads_this_file.begin();
            while (it_vec != pending_reads_this_file.end()) {
                auto tuple = *it_vec;
                int pending_tid = std::get<0>(tuple);
                int fd = std::get<1>(tuple);
                size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
                size_t count = std::get<2>(tuple);
#ifdef CAPIOLOG
                logfile << "pending read tid fd offset count " << tid << " " << fd << " " << process_offset <<" "<< count << std::endl;
#endif
                bool is_getdents = std::get<3>(tuple);
                handle_pending_read(pending_tid, fd, process_offset, count, is_getdents);
                it_vec = pending_reads_this_file.erase(it_vec);
            }
            pending_reads.erase(it);
        }
        if (c_file.is_dir())
            reply_remote_stats(path);
        //TODO: error if seek are done and also do this on exit
        handle_pending_remote_reads(path, c_file.get_sector_end(0), true);
        handle_pending_remote_nfiles(path);
        c_file.commit();
    }

    --c_file.n_opens;
#ifdef CAPIOLOG
    logfile << "capio close n links " << c_file.n_links << " n opens " << c_file.n_opens << std::endl;;
#endif
    if (c_file.n_opens == 0 && c_file.n_links <= 0)
        delete_file(path, rank);
    std::string offset_name = "offset_" + std::to_string(tid) +  "_" + std::to_string(fd);
    if (shm_unlink(offset_name.c_str()) == -1)
        err_exit("shm_unlink " + offset_name + " in handle_close");
    processes_files[tid].erase(fd);
    processes_files_metadata[tid].erase(fd);
    c_file.remove_fd(tid, fd);
}


void handle_close(char* str, char* p, int rank) {
    int tid, fd;
    sscanf(str, "clos %d %d", &tid, &fd);
#ifdef CAPIOLOG
    logfile << "handle close " << tid << " " << fd << std::endl;
#endif
    handle_close(tid, fd, rank);
}

#endif // CAPIO_SERVER_HANDLERS_CLOSE_HPP

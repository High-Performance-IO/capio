#ifndef CAPIO_WRITE_HPP
#define CAPIO_WRITE_HPP
void handle_write(const char* str, int rank) {
    //check if another process is waiting for this data
    std::string request;
    int tid, fd;
    off_t base_offset;
    off64_t count, data_size;
    std::istringstream stream(str);
    stream >> request >> tid >> fd >> base_offset >> count;
    data_size = base_offset + count;
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);

    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    if (c_file.buf_to_allocate()) {
#ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
#endif
        c_file.create_buffer(path, true);
    }
    off64_t file_shm_size = c_file.get_buf_size();
    auto* data_buf = data_buffers[tid].first;
    size_t n_reads = count / WINDOW_DATA_BUFS;
    size_t r = count % WINDOW_DATA_BUFS;
    size_t i = 0;
    char* p;
    if (data_size > file_shm_size) {

#ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size " << data_size << " " << file_shm_size << std::endl;
#endif
        p = expand_memory_for_file(path, data_size, c_file);
    }
    p = c_file.get_buffer();
    p = p + base_offset;
#ifdef CAPIOLOG
    logfile << "debug handle_write 0 " << std::endl;
#endif
    while (i < n_reads) {
#ifdef CAPIOLOG
        logfile << "debug handle_write 2 " << std::endl;
#endif
        data_buf->read(p + i * WINDOW_DATA_BUFS);
        ++i;
    }
#ifdef CAPIOLOG
    logfile << "debug handle_write 3 " << std::endl;
#endif
    if (r)
        data_buf->read(p + i * WINDOW_DATA_BUFS, r);

#ifdef CAPIOLOG
    logfile << "debug handle_write 4 " << std::endl;
#endif

    int pid = pids[tid];
    writers[pid][path] = true;
#ifdef CAPIOLOG
    logfile << "insert sector " << base_offset << ", " << data_size << std::endl;
#endif
    c_file.insert_sector(base_offset, data_size);
#ifdef CAPIOLOG
    c_file.print(logfile);
        logfile << "handle write tid fd " << tid << " " << fd << std::endl;
		logfile << "path " << path << std::endl;
#endif
    sem_wait(&files_metadata_sem);
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(rank, path, tid);
        sem_post(&files_metadata_sem);
        //TODO: it works only if there is one prod per file
        update_dir(tid, path, rank);
    }
    else
        sem_post(&files_metadata_sem);
    sem_wait(&files_metadata_sem);
    std::string mode = c_file.get_mode();
    sem_post(&files_metadata_sem);
    auto it = pending_reads.find(path);
#ifdef CAPIOLOG
    logfile << "mode is " << mode << std::endl;
#endif
    //sem_wait(sems_write[tid]);
    if (it != pending_reads.end() && mode == "append") {
#ifdef CAPIOLOG
        logfile << "There were pending reads for" << path << std::endl;
#endif
        auto& pending_reads_this_file = it->second;
        auto it_vec = pending_reads_this_file.begin();
        while (it_vec != pending_reads_this_file.end()) {
            auto tuple = *it_vec;
            int pending_tid = std::get<0>(tuple);
            int fd = std::get<1>(tuple);
            size_t process_offset = *std::get<1>(processes_files[pending_tid][fd]);
            size_t count = std::get<2>(tuple);
            sem_wait(&files_metadata_sem);
            size_t file_size = files_metadata[path]->get_stored_size();
            sem_post(&files_metadata_sem);
#ifdef CAPIOLOG
            logfile << "pending read offset " << process_offset << " count " << count << " file_size " << file_size << std::endl;
#endif
            if (process_offset + count <= file_size) {
#ifdef CAPIOLOG
                logfile << "handling this pending read"<< std::endl;
#endif
                handle_pending_read(pending_tid, fd, process_offset, count, std::get<3>(tuple));
                it_vec = pending_reads_this_file.erase(it_vec);
            }
            else
                ++it_vec;
        }
    }
    //sem_post(sems_write[tid]);

    if (mode == "append") {
#ifdef CAPIOLOG
        logfile << "handle write serving remote pending reads" << std::endl;
#endif
        handle_pending_remote_reads(path, data_size, false);
    }


}

#endif //CAPIO_WRITE_HPP

#ifndef CAPIO_SERVER_HANDLERS_COMMON_HPP
#define CAPIO_SERVER_HANDLERS_COMMON_HPP

void init_process(int tid) {

#ifdef CAPIOLOG
    logfile << "init process tid " << std::to_string(tid) << std::endl;
#endif
    if (sems_write->find(tid) == sems_write->end()) {
#ifdef CAPIOLOG
        logfile << "init process tid inside if " << std::to_string(tid) << std::endl;
#endif
        //sems_response[tid] = sem_open(("sem_response_read" + std::to_string(tid)).c_str(), O_RDWR);
        /*if (sems_response[tid] == SEM_FAILED) {
            err_exit("error creating sem_response_read" + std::to_string(tid));
        }
        */
        (*sems_write)[tid] = sem_open(("sem_write" + std::to_string(tid)).c_str(), O_RDWR);
        if ((*sems_write)[tid] == SEM_FAILED) {
            err_exit("error creating sem_write" + std::to_string(tid), logfile);
        }
        Circular_buffer<off_t>* cb = new Circular_buffer<off_t>("buf_response" + std::to_string(tid), 8 * 1024 * 1024, sizeof(off_t));
        response_buffers.insert({tid, cb});
        std::string shm_name = "capio_write_data_buffer_tid_" + std::to_string(tid);
        auto* write_data_cb = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
        shm_name = "capio_read_data_buffer_tid_" + std::to_string(tid);
        auto* read_data_cb = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
        data_buffers.insert({tid, {write_data_cb, read_data_cb}});
        //caching_info[tid].first = (int*) get_shm("caching_info" + std::to_string(tid));
        //caching_info[tid].second = (int*) get_shm("caching_info_size" + std::to_string(tid));

    }
#ifdef CAPIOLOG
    logfile << "end init process tid " << std::to_string(tid) << std::endl;
#endif

}

void send_data_to_client(int tid, char* buf, long int count) {
    auto* data_buf = data_buffers[tid].second;
    size_t n_writes = count / WINDOW_DATA_BUFS;
    size_t r = count % WINDOW_DATA_BUFS;
    size_t i = 0;
    while (i < n_writes) {
        data_buf->write(buf + i * WINDOW_DATA_BUFS);
        ++i;
    }
    if (r)
        data_buf->write(buf + i * WINDOW_DATA_BUFS, r);
}


/*
 * Unlink resources in shared memory of the thread with thread id = tid
 * To be called only when the client thread terminates
 */

void free_resources(int tid) {
    std::string sem_write_shm_name;
    auto it_resp = response_buffers.find(tid);
    if (it_resp != response_buffers.end()) {
#ifdef CAPIOLOG
        logfile << "cleaning response buffer " << tid << std::endl;
#endif
        it_resp->second->free_shm();
        delete it_resp->second;
        response_buffers.erase(it_resp);
    }

    if (sems_write->find(tid) != sems_write->end()) {
#ifdef CAPIOLOG
        logfile << "cleaning sem_write" << tid << std::endl;
#endif
        sem_write_shm_name = "sem_write" + std::to_string(tid);
        if (sem_unlink(sem_write_shm_name.c_str()) == -1)
            err_exit("sem_unlink " + sem_write_shm_name + "in sig_term_handler", logfile);
    }

    auto it = data_buffers.find(tid);
    if (it != data_buffers.end()) {
#ifdef CAPIOLOG
        logfile << "cleaning data buffer " << tid << std::endl;
#endif
        it->second.first->free_shm();
        it->second.second->free_shm();
        data_buffers.erase(it);
    }
}

std::unordered_set<std::string> get_paths_opened_files(pid_t tid) {
    std::unordered_set<std::string> set;
    for (auto& it : processes_files_metadata[tid])
        set.insert(it.second);
    return set;
}

void handle_pending_remote_nfiles(std::string path) {
#ifdef CAPIOLOG
    logfile << "handle pending remote nfiles" << std::endl;;
#endif
    if (sem_wait(&clients_remote_pending_nfiles_sem) == -1)
        err_exit("sem_wait clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles", logfile);
    for (auto& p : clients_remote_pending_nfiles) {
        std::string app = p.first;
        std::list<struct remote_n_files*>& app_pending_nfiles = p.second;
        auto it = app_pending_nfiles.begin();
        std::list<struct remote_n_files*>::iterator next_it;
        while (it != app_pending_nfiles.end()) {
            std::string prefix = (*it)->prefix;
            std::unordered_set<std::string> &files = files_sent[app];
            if (sem_wait(&files_location_sem) == -1)
                err_exit("sem_wait files_location_sem in handle_pending_remote_nfiles", logfile);
            auto it_fs = files_location.find(path);
            next_it =  std::next(it);
            if (files.find(path) == files.end() && it_fs != files_location.end() && strcmp(std::get<0>(it_fs->second), node_name) == 0 && path.compare(0, prefix.length(), prefix) == 0) {
                if (sem_post(&files_location_sem) == -1)
                    err_exit("sem_post files_location_sem in handle_pending_remote_nfiles", logfile);
                (*it)->files_path->push_back(path);
                files.insert(path);
                if ((*it)->files_path->size() == (*it)->n_files) {
#ifdef CAPIOLOG
                    logfile << "waking up thread " << std::endl;;
#endif
                    sem_t* sem = (*it)->sem;
                    app_pending_nfiles.erase(it);
                    if (sem_post(sem) == -1)
                        err_exit("sem_post sem in handle_pending_remote_nfiles", logfile);
                }
            }
            else {
                if (sem_post(&files_location_sem) == -1)
                    err_exit("sem_post files_location_sem in handle_pending_remote_nfiles", logfile);
            }

            it = next_it;
        }
    }
    if (sem_post(&clients_remote_pending_nfiles_sem) == -1)
        err_exit("sem_post clients_remote_pending_nfiles_sem in handle_pending_remote_nfiles", logfile);
}


void open_files_metadata(int rank, int* fd_files_location) {
    std::string rank_str = std::to_string(rank);
    std::string file_name = "files_location_" + rank_str + ".txt";
    int fd;
    if ((fd = open(file_name.c_str(), O_WRONLY|O_APPEND|O_CREAT, 0664)) == -1) {
        logfile << "writer error opening file, errno = " << errno << " strerror(errno): " << strerror(errno) << std::endl;
        MPI_Finalize();
        exit(1);
    }
    *fd_files_location = fd;
}

bool data_avaiable(const char* path_c, long int offset, long int nbytes_requested, long int file_size) {
    return (offset + nbytes_requested <= file_size);
}

#endif // CAPIO_SERVER_HANDLERS_COMMON_HPP

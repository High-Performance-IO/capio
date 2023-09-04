#ifndef CAPIO_READ_HPP
#define CAPIO_READ_HPP


void handle_pending_read(int tid, int fd, long int process_offset, long int count, bool is_getdents) {

    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    if (c_file.buf_to_allocate()) {
#ifdef CAPIOLOG
        logfile << "allocating file " << path << std::endl;
#endif
        c_file.create_buffer(path, false);
    }
    char* p = c_file.get_buffer();
    sem_post(&files_metadata_sem);
    off64_t end_of_sector = c_file.get_sector_end(process_offset);
    off64_t end_of_read = process_offset + count;
    size_t bytes_read;
    if (end_of_sector > end_of_read) {
        end_of_sector = end_of_read;
        bytes_read = count;
    }
    else
        bytes_read = end_of_sector - process_offset;
    if (is_getdents) {
        sem_wait(&files_metadata_sem);
        off64_t dir_size = c_file.get_stored_size();
        sem_post(&files_metadata_sem);
        off64_t n_entries = dir_size / theoretical_size_dirent64;
        char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size);
        end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
        response_buffers[tid]->write(&end_of_sector);
        send_data_to_client(tid, p_getdents + process_offset, end_of_sector - process_offset);
        free(p_getdents);
    }
    else {
        response_buffers[tid]->write(&end_of_sector);
        send_data_to_client(tid, p + process_offset, bytes_read);
    }
    //*processes_files[tid][fd].second += count;
    //TODO: check if the file was moved to the disk
}



void handle_local_read(int tid, int fd, off64_t count, bool dir, bool is_getdents, bool is_prod) {
#ifdef CAPIOLOG
    logfile << "handle local read" << std::endl;
#endif
    if (sem_wait(&handle_local_read_sem) == -1)
        err_exit("sem_wait handle_local_read_sem in handle_local_read", logfile);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file & c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
    int pid = pids[tid];
    bool writer = writers[pid][path];
    off64_t end_of_sector = c_file.get_sector_end(process_offset);
#ifdef CAPIOLOG
    logfile << "Am I a writer? " << writer << std::endl;
		logfile << "process offset " << process_offset << std::endl;
		logfile << "count " << count << std::endl;
		logfile << "end of sector" << end_of_sector << std::endl;
		c_file.print(logfile);
#endif
    off64_t end_of_read = process_offset + count;
    //off64_t nreads;
    std::string committed = c_file.get_committed();
    std::string mode = c_file.get_mode();
    if (mode != "append" && !c_file.complete && !writer && !is_prod) {
#ifdef CAPIOLOG
        logfile << "add pending reads 1" << std::endl;
			logfile << "mode " << mode << std::endl;
			logfile << "file complete " << c_file.complete << std::endl;
#endif
        pending_reads[path].push_back(std::make_tuple(tid, fd, count, is_getdents));
    }
    else if (end_of_read > end_of_sector) {
#ifdef CAPIOLOG
        logfile << "Is the file completed? " << c_file.complete << std::endl;
#endif
        if (!is_prod && !writer && !c_file.complete) {
#ifdef CAPIOLOG
            logfile << "add pending reads 2" << std::endl;
#endif
            pending_reads[path].push_back(std::make_tuple(tid, fd, count, is_getdents));
        }
        else {
            //nreads = end_of_sector;
            sem_wait(&files_metadata_sem);
            if (c_file.buf_to_allocate()) {

#ifdef CAPIOLOG
                logfile << "allocating file " << path << std::endl;
#endif
                c_file.create_buffer(path, false);
            }
            char* p = c_file.get_buffer();
            if (is_getdents) {
                off64_t dir_size = c_file.get_stored_size();
                sem_post(&files_metadata_sem);
                off64_t n_entries = dir_size / theoretical_size_dirent64;
                char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
                response_buffers[tid]->write(&end_of_sector);
                send_data_to_client(tid, p_getdents + process_offset, end_of_sector - process_offset);
                free(p_getdents);
            }
            else {
                sem_post(&files_metadata_sem);
#ifdef CAPIOLOG
                logfile << "debug bbb end_of_sector " << end_of_sector << " nreads " << nreads << " count " << count << " process_offset " << process_offset << std::endl;
#endif
                response_buffers[tid]->write(&end_of_sector);
                send_data_to_client(tid, p + process_offset, end_of_sector - process_offset);
            }
        }
    }
    else {
        sem_wait(&files_metadata_sem);
        if (c_file.buf_to_allocate()) {
#ifdef CAPIOLOG
            logfile << "allocating file " << path << std::endl;
#endif
            c_file.create_buffer(path, false);
        }
        char* p = c_file.get_buffer();
        size_t bytes_read;
        bytes_read = count;
#ifdef CAPIOLOG
        logfile << "debug aaa end_of_sector " << end_of_sector << " bytes_read " << bytes_read << " count " << count << " process_offset " << process_offset << std::endl;
#endif
        if (is_getdents) {
            off64_t dir_size = c_file.get_stored_size();
            sem_post(&files_metadata_sem);
            off64_t n_entries = dir_size / theoretical_size_dirent64;
            char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size);
            end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
            response_buffers[tid]->write(&end_of_read);
            send_data_to_client(tid, p_getdents + process_offset, bytes_read);
            free(p_getdents);
        }
        else {
            sem_post(&files_metadata_sem);
            response_buffers[tid]->write(&end_of_read);
            send_data_to_client(tid, p + process_offset, bytes_read);
        }
    }
#ifdef CAPIOLOG
    logfile << "process offset " << process_offset << std::endl;
#endif
    if (sem_post(&handle_local_read_sem) == -1)
        err_exit("sem_post handle_local_read_sem in handle_local_read", logfile);
}


/*
 * Multithread function
 */



bool read_from_local_mem(int tid, off64_t process_offset, off64_t end_of_read,
                         off64_t end_of_sector, off64_t count, std::string path) {
#ifdef CAPIOLOG
    logfile << "reading from local memory" << std::endl;
#endif
    bool res = false;
    if (end_of_read <= end_of_sector) {
        Capio_file& c_file = *files_metadata[path];
        sem_wait(&files_metadata_sem);
        if (c_file.buf_to_allocate()) {
#ifdef CAPIOLOG
            logfile << "allocating file " << path << std::endl;
#endif
            c_file.create_buffer(path, false);
        }
        char* p = c_file.get_buffer();
        sem_post(&files_metadata_sem);
        response_buffers[tid]->write(&end_of_read);
        send_data_to_client(tid, p + process_offset, count);
        res = true;
    }
    return res;
}



/*
 * Multithread function
 */

void handle_remote_read(int tid, int fd, off64_t count, int rank, bool dir, bool is_getdents) {
#ifdef CAPIOLOG
    logfile << "handle remote read before sem_wait" << std::endl;
#endif
    //before sending the request to the remote node, it checks
    //in the local cache

    if (sem_wait(&handle_remote_read_sem) == -1)
        err_exit("sem_wait handle_remote_read_sem in handle_remote_read", logfile);

    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    size_t real_file_size = c_file.real_file_size;
    sem_post(&files_metadata_sem);
    off64_t process_offset = *std::get<1>(processes_files[tid][fd]);
    off64_t end_of_read = process_offset + count;
    off64_t end_of_sector = c_file.get_sector_end(process_offset);
#ifdef CAPIOLOG
    logfile << "complete " << c_file.complete << " end_of_read " << end_of_read << std::endl;
		logfile << " end_of_sector " << end_of_sector << " real_file_size " << real_file_size << std::endl;
#endif
    std::size_t eos;
    if (end_of_sector == -1)
        eos = 0;
    else
        eos = end_of_sector;
    if (c_file.complete && (end_of_read <= end_of_sector || eos == real_file_size)) {
        handle_local_read(tid, fd, count, dir, is_getdents, true);
        if (sem_post(&handle_remote_read_sem) == -1)
            err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);

        return;
    }
    bool res = read_from_local_mem(tid, process_offset, end_of_read, end_of_sector, count, path); //when is not complete but mode = append
    if (res) { // it means end_of_read < end_of_sector
        if (sem_post(&handle_remote_read_sem) == -1)
            err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);
        return;
    }

    // If it is not in cache then send the request to the remote node
    const char* msg;
    std::string str_msg;
    int dest = nodes_helper_rank[std::get<0>(files_location[processes_files_metadata[tid][fd]])];
    size_t offset = *std::get<1>(processes_files[tid][fd]);
    str_msg = "read " + processes_files_metadata[tid][fd] + " " + std::to_string(rank) + " " + std::to_string(offset) + " " + std::to_string(count);
    msg = str_msg.c_str();
#ifdef CAPIOLOG
    logfile << "handle remote read" << std::endl;
		logfile << "msg sent " << msg << std::endl;
		logfile << processes_files_metadata[tid][fd] << std::endl;
		logfile << "dest " << dest << std::endl;
		logfile << "rank" << rank << std::endl;
#endif
    MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
    my_remote_pending_reads[processes_files_metadata[tid][fd]].push_back(std::make_tuple(tid, fd, count, is_getdents));
    if (sem_post(&handle_remote_read_sem) == -1)
        err_exit("sem_post handle_remote_read_sem in handle_remote_read", logfile);
}


bool handle_nreads(std::string path, std::string app_name, int dest) {
    bool success = false;

    long int pos = match_globs(path, &metadata_conf_globs);
    if (pos != -1) {
#ifdef CAPIOLOG
        logfile << "glob matched" << std::endl;
#endif
        std::string glob = std::get<0>(metadata_conf_globs[pos]);
        std::size_t batch_size = std::get<5>(metadata_conf_globs[pos]);
        if (batch_size > 0) {
            char* msg = (char*) malloc(sizeof(char) * (512 + PATH_MAX));
            sprintf(msg, "nrea %zu %s %s %s", batch_size, app_name.c_str(), glob.c_str(), path.c_str());
            MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
            success = true;
#ifdef CAPIOLOG
            logfile << "handling nreads, msg: " << msg << " msg size " << strlen(msg) + 1 << std::endl;
#endif
            free(msg);
            return success;
        }
    }
    return success;
}


void* wait_for_file(void* pthread_arg) {
    struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*) pthread_arg;
    int tid = metadata->tid;
    int fd = metadata-> fd;
    off64_t count = metadata->count;
    bool dir = metadata->dir;
    bool is_getdents = metadata->is_getdents;
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::string path_to_check(processes_files_metadata[tid][fd]);
    loop_check_files_location(path_to_check, rank);

    //check if the file is local or remote
    if (strcmp(std::get<0>(files_location[path_to_check]), node_name) == 0) {
        handle_local_read(tid, fd, count, dir, is_getdents, false);
    }
    else {
        sem_wait(&files_metadata_sem);
        Capio_file& c_file = *files_metadata[path_to_check];
        sem_post(&files_metadata_sem);
        if (!c_file.complete) {
            auto it = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                res = handle_nreads(path_to_check, app_name, nodes_helper_rank[std::get<0>(files_location[path_to_check])]);
            }
            if (res) {
                if (sem_wait(&handle_remote_read_sem) == -1)
                    err_exit("sem_wait handle_remote_read_sem in wait_for_file", logfile);
                my_remote_pending_reads[path_to_check].push_back(std::make_tuple(tid, fd, count, is_getdents));
                if (sem_post(&handle_remote_read_sem) == -1)
                    err_exit("sem_post handle_remote_read_sem in wait_for_file", logfile);
                return nullptr;
            }
        }
#ifdef CAPIOLOG
        logfile << "handle remote read in wait for file" << std::endl;
			logfile << "path to check " << path_to_check << std::endl;
#endif
        handle_remote_read(tid, fd, count, rank, dir, is_getdents);
    }

    free(metadata);
    return nullptr;
}

void handle_read(char* str, int rank, bool dir, bool is_getdents) {
#ifdef CAPIOLOG
    logfile << "handle read str" << str << std::endl;
#endif
    std::string request;
    int tid, fd;
    off64_t count;
    std::istringstream stream(str);
    stream >> request >> tid >> fd >> count;
    std::string path = processes_files_metadata[tid][fd];
    bool is_prod = is_producer(tid, path);
    if (files_location.find(path) == files_location.end() && !is_prod) {
        bool found = check_file_location(rank, processes_files_metadata[tid][fd]);
        if (!found) {
            //launch a thread that checks when the file is created
            pthread_t t;
            struct wait_for_file_metadata* metadata = (struct wait_for_file_metadata*)  malloc(sizeof(wait_for_file_metadata));
            metadata->tid = tid;
            metadata->fd = fd;
            metadata->count = count;
            metadata->dir = dir;
            metadata->is_getdents = is_getdents;
            int res = pthread_create(&t, NULL, wait_for_file, (void*) metadata);
            if (res != 0) {
                logfile << "error creation of capio server thread (handle read wait for file)" << std::endl;
                MPI_Finalize();
                exit(1);
            }

            return;
        }
    }

    if (is_prod || strcmp(std::get<0>(files_location[path]), node_name) == 0 || *capio_dir == path) {
        handle_local_read(tid, fd, count, dir, is_getdents, is_prod);
    }
    else {
        sem_wait(&files_metadata_sem);
        Capio_file& c_file = *files_metadata[path];
        sem_post(&files_metadata_sem);
        if (!c_file.complete) {
            auto it = apps.find(tid);
            bool res = false;
            if (it != apps.end()) {
                std::string app_name = it->second;
                if (!dir)
                    res = handle_nreads(path, app_name, nodes_helper_rank[std::get<0>(files_location[path])]);
            }
            if (res) {
                if (sem_wait(&handle_remote_read_sem) == -1)
                    err_exit("sem_wait handle_remote_read_sem in handle_read", logfile);
                my_remote_pending_reads[path].push_back(std::make_tuple(tid, fd, count, is_getdents));
                if (sem_post(&handle_remote_read_sem) == -1)
                    err_exit("sem_post handle_remote_read_sem in handle_read", logfile);
                return;
            }
        }
#ifdef CAPIOLOG
        logfile << "before handle remote read handle read" << std::endl;
#endif

        handle_remote_read(tid, fd, count, rank, dir, is_getdents);
    }
}
/*
 *	Multithreaded function
 */

void solve_remote_reads(size_t bytes_received, size_t offset, size_t file_size, const char* path_c, bool complete) {
#ifdef CAPIOLOG
    logfile << " solve remote reads before semwait " << std::endl;
#endif
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path_c];
    sem_post(&files_metadata_sem);
    c_file.real_file_size = file_size;
#ifdef CAPIOLOG
    logfile << "insert offset " << offset << " bytes_received " << bytes_received << std::endl;
#endif
    c_file.insert_sector(offset, offset + bytes_received);
    c_file.complete = complete;
    std::string path(path_c);
    int tid, fd;
    long int count; //TODO: diff between count and bytes_received
    if (sem_wait(&handle_remote_read_sem) == -1)
        err_exit("sem_wait handle_remote_read_sem in solve_remote_reads", logfile);
    std::list<std::tuple<int, int, long int, bool>>& list_remote_reads = my_remote_pending_reads[path];
    auto it = list_remote_reads.begin();
    std::list<std::tuple<int, int, long int, bool>>::iterator prev_it;
    off64_t end_of_sector;
    while (it != list_remote_reads.end()) {
        tid = std::get<0>(*it);
        fd = std::get<1>(*it);
        count = std::get<2>(*it);
        bool is_getdent = std::get<3>(*it);
        size_t fd_offset = *std::get<1>(processes_files[tid][fd]);
        if (complete || fd_offset + count <= offset + bytes_received) {
#ifdef CAPIOLOG
            logfile << "handling others remote reads fd_offset " << fd_offset << " count " << count << " offset " << offset << " bytes received " << bytes_received << std::endl;
#endif
            //this part is equals to the local read (TODO: function)
            end_of_sector = c_file.get_sector_end(fd_offset);
#ifdef CAPIOLOG
            logfile << "end of sector " << end_of_sector << std::endl;
#endif
            if (c_file.buf_to_allocate()) {
#ifdef CAPIOLOG
                logfile << "allocating file " << path << std::endl;
#endif
                c_file.create_buffer(path, false);
            }
            char* p = c_file.get_buffer();

            size_t bytes_read;
            off64_t end_of_read = fd_offset + count;
            if (end_of_sector > end_of_read) {
                end_of_sector = end_of_read;
                bytes_read = count;
            }
            else
                bytes_read = end_of_sector - fd_offset;

            if (is_getdent) {
                sem_wait(&files_metadata_sem);
                off64_t dir_size = c_file.get_stored_size();
                sem_post(&files_metadata_sem);
                off64_t n_entries = dir_size / theoretical_size_dirent64;
                char* p_getdents = (char*) malloc(n_entries * sizeof(char) * dir_size);
                end_of_sector = convert_dirent64_to_dirent(p, p_getdents, dir_size);
                response_buffers[tid]->write(&end_of_sector);
                send_data_to_client(tid, p_getdents + fd_offset, bytes_read);
                free(p_getdents);
            }
            else {
                response_buffers[tid]->write(&end_of_sector);
                send_data_to_client(tid, p + fd_offset, bytes_read);
            }

            if (it == list_remote_reads.begin()) {
                list_remote_reads.erase(it);
                it = list_remote_reads.begin();
            }
            else {
                list_remote_reads.erase(it);
                it = std::next(prev_it);
            }
        }
        else {
            prev_it = it;
            ++it;
        }
    }
    if (sem_post(&handle_remote_read_sem) == -1)
        err_exit("sem_post handle_remote_read_sem in solve_remote_reads", logfile);
}

void handle_remote_read(char* str, char* p, int rank) {
    size_t bytes_received, offset, file_size;
    char path_c[PATH_MAX];
    int complete_tmp;
    sscanf(str, "ream %s %zu %zu %d %zu", path_c, &bytes_received, &offset, &complete_tmp, &file_size);
#ifdef CAPIOLOG
    logfile << "serving the remote read: " << str << std::endl;
#endif
    bool complete = complete_tmp;
    solve_remote_reads(bytes_received, offset, file_size, path_c, complete);
}


#endif //CAPIO_READ_HPP

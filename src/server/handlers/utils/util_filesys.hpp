#ifndef CAPIO_UTIL_FILESYS_HPP
#define CAPIO_UTIL_FILESYS_HPP

void create_file(std::string path, bool is_dir, off64_t init_size) {
    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');
    shm_name = shm_name.substr(1);
    std::string committed, mode, app_name;
    long int n_files;
    auto it = metadata_conf.find(path);
    Capio_file* p_capio_file;
    if (it == metadata_conf.end()) {
        long int pos = match_globs(path, &metadata_conf_globs);
        if (pos == -1) {
#ifdef CAPIOLOG
            logfile << "creating file without conf file " << path << std::endl;
#endif
            if (is_dir) {
                init_size = dir_initial_size;
            }
#ifdef CAPIOLOG
            logfile << "init size " << init_size << std::endl;
#endif
            if (sem_wait(&files_metadata_sem) == -1)
                err_exit("sem_wait 1 files_metadata_sem in create_file", logfile);
            p_capio_file = new Capio_file(is_dir, false, init_size, logfile);
            files_metadata[path] = p_capio_file;
            if (sem_post(&files_metadata_sem) == -1)
                err_exit("sem_post 1 files_metadata_sem in create_file", logfile);
        }
        else {
            auto& quintuple = metadata_conf_globs[pos];
            std::string glob = std::get<0>(quintuple);
            committed = std::get<1>(quintuple);
            mode = std::get<2>(quintuple);
            app_name = std::get<3>(quintuple);
            n_files = std::get<4>(quintuple);
#ifdef CAPIOLOG
            logfile << "creating file using globbing " << path << std::endl;
			logfile << "committed " << committed << " mode " << mode << "app name " << app_name << " nfiles " << n_files <<  std::endl;
#endif
            if (in_dir(path, glob)) {
                n_files = 0;
            }

            if (n_files > 0) {
                init_size = dir_initial_size;
                is_dir = true;
            }

            if (sem_wait(&files_metadata_sem) == -1)
                err_exit("sem_wait 2 files_metadata_sem in create_file", logfile);
            bool permanent = std::get<6>(metadata_conf_globs[pos]);
            long int n_close = std::get<7>(metadata_conf_globs[pos]);
#ifdef CAPIOLOG
            logfile << "creating file " << path << " permanent " << permanent << " dir " << is_dir << std::endl;
#endif
            p_capio_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, logfile, n_close);
            files_metadata[path] = p_capio_file;
            if (sem_post(&files_metadata_sem) == -1)
                err_exit("sem_post 2 files_metadata_sem in create_file", logfile);
            metadata_conf[path] = std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
        }
    }
    else {
        committed = std::get<0>(it->second);
        mode = std::get<1>(it->second);
        n_files = std::get<3>(it->second);
#ifdef CAPIOLOG
        logfile << "creating file " << path << std::endl;
		logfile << "committed " << committed << " mode " << mode << std::endl;
#endif
        if (n_files > 0) {
            is_dir = true;
            init_size = dir_initial_size;
        }

        if (sem_wait(&files_metadata_sem) == -1)
            err_exit("sem_wait 3 files_metadata_sem in create_file", logfile);
        bool permanent =  std::get<4>(it->second);
        long int n_close = std::get<5>(it->second);
#ifdef CAPIOLOG
        logfile << "creating file " << path << " permanent " << permanent << " dir " << is_dir << std::endl;
#endif
        p_capio_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, logfile, n_close);
        files_metadata[path] = p_capio_file;
        if (sem_post(&files_metadata_sem) == -1)
            err_exit("sem_post 3 files_metadata_sem in create_file", logfile);
    }
}

void reply_remote_stats(std::string path) {
#ifdef CAPIOLOG
    logfile << "check reply remote stats " << path << std::endl;
#endif
    auto it_client = clients_remote_pending_stat.find(path);
    if (it_client !=  clients_remote_pending_stat.end()) {
        for (sem_t* sem : it_client->second) {
            if (sem_post(sem)  == -1)
                err_exit("error sem_post sem in reply_remote_stats", logfile);
#ifdef CAPIOLOG
            logfile << "reply remote stat" << std::endl;
#endif
        }
        clients_remote_pending_stat.erase(path);
    }
}

void handle_pending_remote_reads(std::string path, off64_t data_size, bool complete) {
    auto it_client = clients_remote_pending_reads.find(path);
    if (it_client !=  clients_remote_pending_reads.end()) {
        std::list<std::tuple<size_t, size_t, sem_t*>>::iterator it_list, prev_it_list;
        it_list = it_client->second.begin();
        while (it_list != it_client->second.end()) {
            off64_t offset = std::get<0>(*it_list);
            off64_t nbytes = std::get<1>(*it_list);
            sem_t* sem = std::get<2>(*it_list);
#ifdef CAPIOLOG
            logfile << "handle serving remote pending reads inside the loop" << std::endl;
#endif
            if (complete || (offset + nbytes < data_size)) {
                if (sem_post(sem) == -1)
                    err_exit("sem_post sem in handle_pending_remote_reads", logfile);
                if (it_list == it_client->second.begin()) {
                    it_client->second.erase(it_list);
                    it_list = it_client->second.begin();
                }
                else {
                    it_client->second.erase(it_list);
                    it_list = std::next(prev_it_list);
                }
            }
            else {
                prev_it_list = it_list;
                ++it_list;
            }
        }
    }
}

/*
 * type == 0 -> regular entry
 * type == 1 -> "." entry
 * type == 2 -> ".." entry
 */

void write_entry_dir(int tid, std::string file_path, std::string dir, int type) {
    std::hash<std::string> hash;
    struct linux_dirent64 ld;
    ld.d_ino = hash(file_path);
    std::string file_name;
    if (type == 0) {
        std::size_t i = file_path.rfind('/');
        if (i == std::string::npos) {
            logfile << "invalid file_path in get_parent_dir_path" << std::endl;
        }
        file_name = file_path.substr(i + 1);
    }
    else if (type == 1) {
        file_name = ".";
    }
    else {
        file_name = "..";
    }

    strcpy(ld.d_name, file_name.c_str());
    long int ld_size = THEORETICAL_SIZE_DIRENT64;
    ld.d_reclen =  ld_size;
    if (sem_wait(&files_metadata_sem) == -1)
        err_exit("sem_wait files_metadata_sem in write_entry_dir", logfile);
    auto it_tuple = files_metadata.find(dir);

    if (it_tuple == files_metadata.end()) {
        logfile << "dir " << dir << " is not present in CAPIO" << std::endl;
        exit(1);
    }

    Capio_file* c_file = it_tuple->second;
    if (c_file->buf_to_allocate()) {
#ifdef CAPIOLOG
        logfile << "allocating file " << dir << std::endl;
#endif
        c_file->create_buffer(dir, true);
    }
    void* file_shm = c_file->get_buffer();
    off64_t file_size = c_file->get_stored_size();
    off64_t data_size = file_size + ld_size; //TODO: check theoreitcal size and sizeof(ld) usage
    off64_t file_shm_size = c_file->get_buf_size();
    ld.d_off =  data_size;
    if (sem_post(&files_metadata_sem) == -1)
        err_exit("sem_post files_metadata_sem in write_entry_dir", logfile);

    if (data_size > file_shm_size) {
#ifdef CAPIOLOG
        logfile << "handle write data_size > file_shm_size" << std::endl;
#endif
        file_shm = expand_memory_for_file(dir, data_size, *c_file);
    }
    if (c_file->is_dir()) {
        ld.d_type = DT_DIR;
    }
    else {
        ld.d_type = DT_REG;
    }
    ld.d_name[DNAME_LENGTH] = '\0';
    memcpy((char*) file_shm + file_size, &ld, sizeof(ld));
    off64_t base_offset = file_size;
#ifdef CAPIOLOG
    logfile << "insert sector for dir" << base_offset << ", " << data_size << std::endl;
#endif
    c_file->insert_sector(base_offset, data_size);
    ++c_file->n_files;
    std::string committed = c_file->get_committed();
    int pid = pids[tid];
    writers[pid][dir] = true;
#ifdef CAPIOLOG
    logfile << "nfiles in dir " << dir << " " << c_file->n_files << " " << c_file->n_files_expected << std::endl;
#endif
    if (c_file->n_files == c_file->n_files_expected) {
#ifdef CAPIOLOG
        logfile << "dir completed " << std::endl;
#endif
        c_file->complete = true;
        reply_remote_stats(dir);
    }

    std::string mode = c_file->get_mode();
    if (mode == "append") {
#ifdef CAPIOLOG
        logfile << "write entry serving remote reads" << std::endl;
#endif
        handle_pending_remote_reads(dir, data_size, c_file->complete);
    }
#ifdef CAPIOLOG
    c_file->print(logfile);
#endif
}

void write_file_location(int rank, std::string path_to_write, int tid) {
#ifdef CAPIOLOG
    logfile << "write file location before, tid " << tid << std::endl;
#endif
    struct flock lock;
    memset(&lock, 0, sizeof(lock));

    int fd = fd_files_location;
    // lock in exclusive mode
    lock.l_type = F_WRLCK;
    // lock entire file
    lock.l_whence = SEEK_SET; // offset base is start of the file
    lock.l_start = 0;         // starting offset is zero
    lock.l_len = 0;           // len is zero, which is a special value representing end
    // of file (no matter how large the file grows in future)
    lock.l_pid = getpid();
    if (fcntl(fd, F_SETLKW, &lock) == -1) { // F_SETLK doesn't block, F_SETLKW does
        logfile << "write " << rank << "failed to lock the file" << std::endl;
    }

    long offset = lseek(fd, 0, SEEK_CUR);
    if (offset == -1)
        err_exit("lseek in write_file_location", "write_file_location", logfile);

    const char* path_to_write_cstr = path_to_write.c_str();
    const char* space_str = " ";
    const size_t len1 = strlen(path_to_write_cstr);
    const size_t len2 = strlen(space_str);
    const size_t len3 = strlen(node_name);
    char *file_location = (char*) malloc(len1 + len2 + len3 + 2); // +2 for  \n and for the null-terminator
    memcpy(file_location, path_to_write_cstr, len1);
    memcpy(file_location + len1, space_str, len2);
    memcpy(file_location + len1 + len2, node_name, len3);
    file_location[len1 + len2 + len3] = '\n';
    file_location[len1 + len2 + len3 + 1] = '\0';
    write(fd, file_location, sizeof(char) * strlen(file_location));
#ifdef CAPIOLOG
    logfile << "file_location writing " << path_to_write << " " << file_location << std::endl;
#endif
    files_location[path_to_write] = std::make_pair(node_name, offset);
    // Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == - 1) {
        logfile << "write " << rank << "failed to unlock the file" << std::endl;
    }

    free(file_location);
    //close(fd); // close the file: would unlock if needed
#ifdef CAPIOLOG
    logfile << "write file location after" << std::endl;
#endif
    return;
}

void update_dir(int tid, std::string file_path, int rank) {
    std::string dir = get_parent_dir_path(file_path, &logfile);
#ifdef CAPIOLOG
    logfile << "update dir " << dir << std::endl;
#endif
    if (sem_wait(&files_metadata_sem) == -1)
        err_exit("sem_wait files_metadata_sem in update_dir", logfile);
    Capio_file* c_file = files_metadata[dir];
    if (c_file->first_write) {
        c_file->first_write = false;
        write_file_location(rank, dir, tid);
    }
    if (sem_post(&files_metadata_sem) == -1)
        err_exit("sem_post files_metadata_sem in update_dir", logfile);
#ifdef CAPIOLOG
    logfile << "before write entry dir" << std::endl;
#endif
    write_entry_dir(tid, file_path, dir, 0);
#ifdef CAPIOLOG
    logfile << "update dir end" << std::endl;
#endif
    return;
}

void update_file_metadata(std::string path, int tid, int fd, int rank, bool is_creat) {
//    void* p_shm;
    //int index = *caching_info[tid].second;
    //caching_info[tid].first[index] = fd;
    //if (on_disk.find(path) == on_disk.end()) {
    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');
    shm_name = shm_name.substr(1);
  //  p_shm = new char[file_initial_size];
#ifdef CAPIOLOG
//    logfile << " address p_shm " << p_shm << std::endl;
#endif
    //caching_info[tid].first[index + 1] = 0;
    //}
    /*else {
        p_shm = nullptr;
        //caching_info[tid].first[index + 1] = 1;
    }
    */
    //TODO: check the size that the user wrote in the configuration file
    off64_t* p_offset = (off64_t*) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
    sem_wait(&files_metadata_sem);

    sem_post(&files_metadata_sem);
    //*caching_info[tid].second += 2;
    sem_wait(&files_metadata_sem);
    auto it = files_metadata.find(path);
    if (it == files_metadata.end()) {
        sem_post(&files_metadata_sem);
        create_file(path, false, get_file_initial_size());
        it = files_metadata.find(path);
    }
    else
        sem_post(&files_metadata_sem);
    Capio_file* p_capio_file = it->second;
    p_capio_file->add_fd(tid, fd);
    ++p_capio_file->n_opens;
    processes_files[tid][fd] = std::make_tuple(p_capio_file, p_offset);//TODO: what happens if a process open the same file twice?
#ifdef CAPIOLOG
    logfile << "capio open n links " << p_capio_file->n_links << " n opens " << p_capio_file->n_opens << std::endl;;
#endif
    processes_files_metadata[tid][fd] = path;
    int pid = pids[tid];
    auto it_files = writers.find(pid);
    if (it_files != writers.end()) {
        auto it_bools = it_files->second.find(path);
        if (it_bools == it_files->second.end()) {
            writers[pid][path] = false;
        }
    }
    else {
        writers[pid][path] = false;
    }

    sem_wait(&files_metadata_sem);
    if (p_capio_file->first_write && is_creat) {
        p_capio_file->first_write = false;
        sem_post(&files_metadata_sem);
        write_file_location(rank, path, tid);
        update_dir(tid, path, rank);
    }
    else
        sem_post(&files_metadata_sem);
}

off64_t create_dir(int tid, const char* pathname, int rank, bool root_dir) {
    off64_t res;
#ifdef CAPIOLOG
    logfile << "handle mkdir " << pathname << std::endl;
#endif
    if (files_location.find(pathname) == files_location.end()) {
        std::string shm_name = pathname;
        std::replace(shm_name.begin(), shm_name.end(), '/', '_');
        shm_name = shm_name.substr(1);
        create_file(pathname, true, dir_initial_size);
        sem_wait(&files_metadata_sem);
        Capio_file& c_file = *files_metadata[pathname];
        if (c_file.first_write) {
            c_file.first_write = false;
            sem_post(&files_metadata_sem);
            //TODO: it works only if there is one prod per file
            if (root_dir) {
                files_location[pathname] =  std::make_pair(node_name, -1);
            }
            else {
                write_file_location(rank, pathname, tid);
                update_dir(tid, pathname, rank);
            }
            write_entry_dir(tid, pathname, pathname, 1);
            std::string parent_dir = get_parent_dir_path(pathname, &logfile);
            write_entry_dir(tid, parent_dir, pathname, 2);
        }
        else
            sem_post(&files_metadata_sem);
        res = 0;
    }
    else {
        res = 1;
    }
#ifdef CAPIOLOG
    logfile << "handle mkdir returning " << res << std::endl;
#endif
    return res;
}




void get_capio_dir() {
    char* val;
    if (capio_dir == nullptr) {
        val = getenv("CAPIO_DIR");
        try {
            if (val == NULL) {
                capio_dir = new std::string(std::filesystem::canonical("."));
            }
            else {
                capio_dir = new std::string(std::filesystem::canonical(val));
            }
        }
        catch (const std::exception& ex) {
            if (val == NULL)
                logfile << "error CAPIO_DIR: current directory not valid" << std::endl;
            else
                logfile << "error CAPIO_DIR: directory " << val << " does not exist" << std::endl;
            exit(1);
        }
        int res = is_directory(capio_dir->c_str());
        if (res == 0) {
            logfile << "dir " << capio_dir << " is not a directory" << std::endl;
            exit(1);
        }
    }
#ifdef CAPIOLOG
    logfile << "capio dir " << *capio_dir << std::endl;
#endif
}


#endif //CAPIO_UTIL_FILESYS_HPP

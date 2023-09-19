#ifndef CAPIO_UTIL_FILESYS_HPP
#define CAPIO_UTIL_FILESYS_HPP

void reply_remote_stats(const std::string& path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    auto it_client = clients_remote_pending_stat.find(path);
    if (it_client != clients_remote_pending_stat.end()) {
        for (sem_t *sem: it_client->second) {
            if (sem_post(sem) == -1)
                ERR_EXIT("error sem_post sem in reply_remote_stats");
        }
        clients_remote_pending_stat.erase(path);
    }
}

void handle_pending_remote_reads(const std::string& path, off64_t data_size, bool complete) {
    START_LOG(gettid(), "call(%s, %ld, %d)", path.c_str(), data_size, static_cast<int>(complete));

    auto it_client = clients_remote_pending_reads.find(path);
    if (it_client != clients_remote_pending_reads.end()) {
        std::list<std::tuple<size_t, size_t, sem_t *>>::iterator it_list, prev_it_list;
        it_list = it_client->second.begin();
        while (it_list != it_client->second.end()) {
            auto& [offset, nbytes, sem] = *it_list;
            if (complete || (offset + nbytes < data_size)) {
                if (sem_post(sem) == -1)
                    ERR_EXIT("sem_post sem in handle_pending_remote_reads");
                if (it_list == it_client->second.begin()) {
                    it_client->second.erase(it_list);
                    it_list = it_client->second.begin();
                } else {
                    it_client->second.erase(it_list);
                    it_list = std::next(prev_it_list);
                }
            } else {
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

void write_entry_dir(int tid, const std::string& file_path, const std::string& dir, int type) {
    START_LOG(tid, "call(file_path=%s, dir=%s, type=%d)", file_path.c_str(), dir.c_str(), type);

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
    } else if (type == 1) {
        file_name = ".";
    } else {
        file_name = "..";
    }

    strcpy(ld.d_name, file_name.c_str());
    long int ld_size = THEORETICAL_SIZE_DIRENT64;
    ld.d_reclen = ld_size;

    Capio_file &c_file = init_capio_file(dir.c_str(), true);
    void *file_shm = c_file.get_buffer();
    off64_t file_size = c_file.get_stored_size();
    off64_t data_size = file_size + ld_size; //TODO: check theoreitcal size and sizeof(ld) usage
    size_t file_shm_size = c_file.get_buf_size();
    ld.d_off = data_size;

    if (data_size > file_shm_size) {
        file_shm = expand_memory_for_file(dir, data_size, c_file);
    }
    if (c_file.is_dir()) {
        ld.d_type = DT_DIR;
    } else {
        ld.d_type = DT_REG;
    }
    ld.d_name[DNAME_LENGTH] = '\0';
    memcpy((char *) file_shm + file_size, &ld, sizeof(ld));
    off64_t base_offset = file_size;

    c_file.insert_sector(base_offset, data_size);
    ++c_file.n_files;
    int pid = pids[tid];
    writers[pid][dir] = true;

    if (c_file.n_files == c_file.n_files_expected) {
        c_file.complete = true;
        reply_remote_stats(dir);
    }

    std::string_view mode = c_file.get_mode();
    if (mode == "append") {
        handle_pending_remote_reads(dir, data_size, c_file.complete);
    }
}

void write_file_location(int rank, const std::string& path_to_write, int tid) {
    START_LOG(tid, "call(rank=%d, path_to_write=%s)", rank, path_to_write.c_str());
    struct flock lock{};
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
        ERR_EXIT("lseek in write_file_location");

    const char *path_to_write_cstr = path_to_write.c_str();
    const char *space_str = " ";
    const size_t len1 = strlen(path_to_write_cstr);
    const size_t len2 = strlen(space_str);
    const size_t len3 = strlen(node_name);
    char *file_location = (char *) malloc(len1 + len2 + len3 + 2); // +2 for  \n and for the null-terminator
    memcpy(file_location, path_to_write_cstr, len1);
    memcpy(file_location + len1, space_str, len2);
    memcpy(file_location + len1 + len2, node_name, len3);
    file_location[len1 + len2 + len3] = '\n';
    file_location[len1 + len2 + len3 + 1] = '\0';
    write(fd, file_location, sizeof(char) * strlen(file_location));

    files_location[path_to_write] = std::make_pair(node_name, offset);
    // Now release the lock explicitly.
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        logfile << "write " << rank << "failed to unlock the file" << std::endl;
    }

    free(file_location);
}

void update_dir(int tid, const std::string& file_path, int rank) {
    START_LOG(tid, "call(file_path=%s, rank=%d)", file_path.c_str(), rank);
    std::string dir = get_parent_dir_path(file_path);
    Capio_file& c_file = get_capio_file(dir.c_str());
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(rank, dir, tid);
    }
    write_entry_dir(tid, file_path, dir, 0);
}

void update_file_metadata(const std::string& path, int tid, int fd, int rank, bool is_creat) {
    START_LOG(tid, "call(path=%s, fd=%d, rank=%d, is_creat=%s)", path.c_str(), fd, rank, is_creat? "true" : "false");

    //TODO: check the size that the user wrote in the configuration file
    off64_t *p_offset = (off64_t *) create_shm("offset_" + std::to_string(tid) + "_" + std::to_string(fd),
                                               sizeof(off64_t));
    //*caching_info[tid].second += 2;
    auto c_file_opt = get_capio_file_opt(path.c_str());
    Capio_file &c_file = (c_file_opt)? c_file_opt->get() : create_capio_file(path, false, get_file_initial_size());
    c_file.open();
    add_capio_file_to_tid(tid, fd, path);
    processes_files[tid][fd] = std::make_tuple(&c_file, p_offset);//TODO: what happens if a process open the same file twice?
    int pid = pids[tid];
    auto it_files = writers.find(pid);
    if (it_files != writers.end()) {
        auto it_bools = it_files->second.find(path);
        if (it_bools == it_files->second.end()) {
            writers[pid][path] = false;
        }
    } else {
        writers[pid][path] = false;
    }
    if (c_file.first_write && is_creat) {
        c_file.first_write = false;
        write_file_location(rank, path, tid);
        update_dir(tid, path, rank);
    }
}

off64_t create_dir(int tid, const char *pathname, int rank, bool root_dir) {
    START_LOG(tid, "call(pathname=%s, rank=%d, root_dir=%s)", pathname, rank, root_dir? "true" : "false");

    if (files_location.find(pathname) == files_location.end()) {
        Capio_file &c_file = create_capio_file(pathname, true, DIR_INITIAL_SIZE);
        if (c_file.first_write) {
            c_file.first_write = false;
            //TODO: it works only if there is one prod per file
            if (root_dir) {
                files_location[pathname] = std::make_pair(node_name, -1);
            } else {
                write_file_location(rank, pathname, tid);
                update_dir(tid, pathname, rank);
            }
            write_entry_dir(tid, pathname, pathname, 1);
            std::string parent_dir = get_parent_dir_path(pathname);
            write_entry_dir(tid, parent_dir, pathname, 2);
        }
        return 0;
    } else {
        return 1;
    }
}

#endif //CAPIO_UTIL_FILESYS_HPP

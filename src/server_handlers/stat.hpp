#ifndef CAPIO_STAT_HPP
#define CAPIO_STAT_HPP
void handle_local_stat(int tid, std::string path) {
    off64_t file_size;
    sem_wait(&files_metadata_sem);
    auto it = files_metadata.find(path);
    Capio_file& c_file = *(it->second);
    sem_post(&files_metadata_sem);
    file_size = c_file.get_file_size();
    off64_t is_dir;
    if (c_file.is_dir())
        is_dir = 0;
    else
        is_dir = 1;
    response_buffers[tid]->write(&file_size);
    response_buffers[tid]->write(&is_dir);
#ifdef CAPIOLOG
    logfile << "file size handle local stat : " << file_size << std::endl;
#endif
}

void handle_remote_stat(int tid, const std::string path, int rank) {
#ifdef CAPIOLOG
    logfile << "handle remote stat before sem_wait" << std::endl;
#endif
    if (sem_wait(&handle_remote_stat_sem) == -1)
        err_exit("sem_wait handle_remote_stat_sem in handle_remote_stat", logfile);
    std::string str_msg;
    int dest = nodes_helper_rank[std::get<0>(files_location[path])];
    str_msg = "stat " + std::to_string(rank) + " " + path;
    const char* msg = str_msg.c_str();
#ifdef CAPIOLOG
    logfile << "handle remote stat" << std::endl;
	logfile << "msg sent " << msg << std::endl;
	logfile << "dest " << dest << std::endl;
	logfile << "rank" << rank << std::endl;
#endif
    MPI_Send(msg, strlen(msg) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
#ifdef CAPIOLOG
    logfile << "remote stat 0" << std::endl;
#endif
    my_remote_pending_stats[path].push_back(tid);
#ifdef CAPIOLOG
    logfile << "remote stat 1" << std::endl;
#endif
    if (sem_post(&handle_remote_stat_sem) == -1)
        err_exit("sem_post handle_remote_stat_sem in handle_remote_stat", logfile);
#ifdef CAPIOLOG
    logfile << "remote stat 2" << std::endl;
#endif

}

void* wait_for_stat(void* pthread_arg) {
    struct wait_for_stat_metadata* metadata = (struct wait_for_stat_metadata*) pthread_arg;
    int tid = metadata->tid;
    const char* path = metadata->path;
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::string path_to_check(path);
#ifdef CAPIOLOG
    logfile << "wait for stat" << std::endl;
#endif
    loop_check_files_location(path_to_check, rank);

    //check if the file is local or remote

    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    std::string mode = c_file.get_mode();
    bool complete = c_file.complete;
    if (complete || strcmp(std::get<0>(files_location[path_to_check]), node_name) == 0 || mode == "append") {
        handle_local_stat(tid, path);
    }
    else {
        handle_remote_stat(tid, path, rank);
    }

    free(metadata);
    return nullptr;
}


void reply_stat(int tid, std::string path, int rank) {
    if (files_location.find(path) == files_location.end()) {
        check_file_location(rank, path);
        if (files_location.find(path) == files_location.end()) {
            //if it is in configuration file then wait otherwise fails

            if ((metadata_conf.find(path) != metadata_conf.end() || match_globs(path, &metadata_conf_globs) != -1) && !is_producer(tid, path)) {
                pthread_t t;
                struct wait_for_stat_metadata* metadata = (struct wait_for_stat_metadata*)  malloc(sizeof(wait_for_stat_metadata));
                metadata->tid = tid;
                strcpy(metadata->path, path.c_str());
                int res = pthread_create(&t, NULL, wait_for_stat, (void*) metadata);
                if (res != 0) {
                    logfile << "error creation of capio server thread wait for stat" << std::endl;
                    MPI_Finalize();
                    exit(1);
                }
            }
            else {
                off64_t file_size;
                file_size = -1;
                response_buffers[tid]->write(&file_size);
#ifdef CAPIOLOG
                logfile << "file size stat : " << file_size << std::endl;
#endif
            }
            return;
        }
    }

    sem_wait(&files_metadata_sem);
    if (files_metadata.find(path) == files_metadata.end()) {
        sem_post(&files_metadata_sem);
        create_file(path, false, file_initial_size);
    }
    else
        sem_post(&files_metadata_sem);

    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    std::string mode = c_file.get_mode();
    bool complete = c_file.complete;
#ifdef CAPIOLOG
    logfile << "node_name : " << node_name << std::endl;
		logfile << " files_location[path]: " << std::get<0>(files_location[path]) << std::endl;
#endif
    if (complete || strcmp(std::get<0>(files_location[path]), node_name) == 0 || mode == "append" || *capio_dir == path) {
        handle_local_stat(tid, path);
    }
    else {
        handle_remote_stat(tid, path, rank);
    }

}


void handle_stat(const char* str, int rank) {
    char path[2048];
    int tid;
    sscanf(str, "stat %d %s", &tid, path);

    init_process(tid);
    reply_stat(tid, path, rank);
}

void handle_fstat(const char* str, int rank) {
    int tid, fd;
    sscanf(str, "fsta %d %d", &tid, &fd);
    std::string path = processes_files_metadata[tid][fd];
#ifdef CAPIOLOG
    logfile << "path " << path << std::endl;
#endif

    reply_stat(tid, path, rank);
}

void handle_stat_reply(const char* str) {
    off64_t size;
    int dir_tmp;
    char path_c[1024];
    sscanf(str, "stam %s %ld %d", path_c, &size, &dir_tmp);
    off64_t dir = dir_tmp;
#ifdef CAPIOLOG
    logfile << "serving the remote stat: " << str << std::endl;
#endif

    auto it = my_remote_pending_stats.find(path_c);
    if (it == my_remote_pending_stats.end())
        exit(1);
    for (int tid : it->second) {
        response_buffers[tid]->write(&size);
        response_buffers[tid]->write(&dir);
    }
    my_remote_pending_stats.erase(it);
}


#endif //CAPIO_STAT_HPP

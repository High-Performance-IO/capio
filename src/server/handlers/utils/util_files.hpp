#ifndef CAPIO_UTIL_FILES_HPP
#define CAPIO_UTIL_FILES_HPP

/*
 * Returns 0 if the file "file_name" does not exists
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
*/
int check_file_location(std::size_t index, int rank, const std::string& path_to_check) {
    START_LOG(gettid(), "call(index=%ld, rank=%d, path_to_check=%s)", index, rank, path_to_check.c_str());

    FILE *fp;
    bool seek_needed;
    char *line = nullptr;
    size_t len = 0;
    ssize_t read;
    int res = 1;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */

    if (index < fd_files_location_reads.size()) {
        fd = std::get<0>(fd_files_location_reads[index]);
        fp = std::get<1>(fd_files_location_reads[index]);
        seek_needed = std::get<2>(fd_files_location_reads[index]);

    } else {
        std::string index_str = std::to_string(index);
        std::string file_name = "files_location_" + index_str + ".txt";
        fp = fopen(file_name.c_str(), "r+");
        if (fp == nullptr) {
            return 0;
        }

        fd = fileno(fp);
        if (fd == -1)
            ERR_EXIT("fileno in check_file_location");
        seek_needed = false;
        fd_files_location_reads.push_back(std::make_tuple(fd, fp, seek_needed));

    }
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        close(fd);
        ERR_EXIT("capio server %d failed to lock the file", rank);
    }
    const char *path_to_check_cstr = path_to_check.c_str();
    bool found = false;

    if (seek_needed) {
        long offset = ftell(fp);
        if (fseek(fp, offset, SEEK_SET) == -1)
            ERR_EXIT("fseek in check_file_location");
    }

    while (!found && (read = getline(&line, &len, fp)) != -1) {

        if (line[0] == '0')
            continue;
        char path[1024]; //TODO: heap memory
        int i = 0;
        while (line[i] != ' ') {
            path[i] = line[i];
            ++i;
        }

        path[i] = '\0';
        char node_str[1024]; //TODO: heap memory
        ++i;
        int j = 0;
        while (line[i] != '\n') {
            node_str[j] = line[i];
            ++i;
            ++j;
        }


        node_str[j] = '\0';
        char *p_node_str = (char *) malloc(sizeof(char) * (strlen(node_str) + 1));
        strcpy(p_node_str, node_str);
        long offset = ftell(fp);
        if (offset == -1)
            ERR_EXIT("ftell in check_file_location");
        if (sem_wait(&files_location_sem) == -1)
            ERR_EXIT("sem_wait files_location_sem in check_file_location");
        files_location[path] = std::make_pair(p_node_str, offset);
        if (sem_post(&files_location_sem) == -1)
            ERR_EXIT("sem_post files_location_sem in check_file_location");

        if (strcmp(path, path_to_check_cstr) == 0) {
            found = true;
        }
        //check if the file is present
    }
    if (found)
        res = 1;
    else {
        std::get<2>(fd_files_location_reads[index]) = true;
        res = 2;
    }
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        ERR_EXIT("reader %d failed to unlock the file", rank);
    }

    return res;
}

bool check_file_location(int my_rank, const std::string& path_to_check) {
    START_LOG(gettid(), "call(my_rank=%d, path_to_check=%s)", my_rank, path_to_check.c_str());

    bool found = false;
    int rank = 0, res = -1;
    while (!found && rank < n_servers) {
        std::string rank_str = std::to_string(rank);
        res = check_file_location(rank, my_rank, path_to_check);
        found = res == 1;
        ++rank;
    }

    return found;
}

void loop_check_files_location(const std::string& path_to_check, int rank) {
    START_LOG(gettid(), "call(path_to_check=%s, rank=%d)", path_to_check.c_str(), rank);

    struct timespec sleepTime, returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 200000;
    bool found = false;
    while (!found) {
        nanosleep(&sleepTime, &returnTime);
        found = check_file_location(rank, path_to_check);
    }


}

#endif //CAPIO_UTIL_FILES_HPP

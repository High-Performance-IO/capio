#ifndef CAPIO_UTIL_DELETE_HPP
#define CAPIO_UTIL_DELETE_HPP

/*
 * Returns 0 if the file "file_name" does not exists
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
*/

int delete_from_file_locations(std::string file_name, std::string path_to_remove, int rank) {
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read = 0;
    int res = 0;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */

    int fd; /* file descriptor to identify a file within a process */
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    fp = fopen(file_name.c_str(), "r+");
    if (fp == NULL) {
        logfile << "capio server " << rank << " failed to open the location file" << std::endl;
        return 0;
    }
    fd = fileno(fp);
    if (fd == -1)
        err_exit("fileno delete_from_file_location", logfile);
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        close(fd);
        err_exit("fcntl lock in delete_from_file_location", logfile);
    }
    const char* path_to_check_cstr = path_to_remove.c_str();
    bool found = false;
    long byte = 0;
    while (read != -1 && !found) {
        byte = ftell(fp);
        if (byte == -1)
            err_exit("ftell delete_from_file_location", logfile);

        read = getline(&line, &len, fp);
        if (read == -1)
            break;
        if (line[0] == '0')
            continue;
        char path[1024]; //TODO: heap memory
        int i = 0;
        while(line[i] != ' ') {
            path[i] = line[i];
            ++i;
        }
        path[i] = '\0';
        if (strcmp(path, path_to_check_cstr) == 0) {
            found = true;
        }
        //check if the file is present
    }
    if (found) {
        char del_char = '0';
        if (fseek(fp, byte, SEEK_SET) == -1)
            err_exit("fseek delete_from_file_location", logfile);
        fwrite(&del_char, sizeof(char), 1, fp);
        res = 1;
#ifdef CAPIOLOG
        logfile << "deleting line" << path_to_remove << std::endl;
#endif
    }
    else
        res = 2;
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        err_exit("fctl unlock in delete_from_file_location", logfile);
    }
    if (fclose(fp) == EOF)
        err_exit("fclose delete_from_file_location", logfile);
    return res;
}

void delete_from_file_locations(std::string path_metadata, long int offset, int my_rank, std::size_t rank) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */
    int fd;
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    if (rank < fd_files_location_reads.size()) {

#ifdef CAPIOLOG
        logfile << "fast remove if " << offset << std::endl;
#endif
        fd = std::get<0>(fd_files_location_reads[rank]);
    }
    else {

#ifdef CAPIOLOG
        logfile << "fast remove else " << offset << std::endl;
#endif
        std::string index_str = std::to_string(rank);
        std::string file_name = "files_location_" + std::to_string(rank) + ".txt";
        FILE* fp = fopen(file_name.c_str(), "r+");
        if (fp == NULL)
            err_exit("fopen " + file_name + " delete_from_file_locations", logfile);
        fd = fileno(fp);
        if (fd == -1)
            err_exit("fileno delete_from_file_locations", logfile);
        fd_files_location_reads.push_back(std::make_tuple(fd, fp, false));
    }
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        err_exit("fcntl delete_from_file_locations", logfile);
    }
#ifdef CAPIOLOG
    logfile << "fast remove offset " << offset << std::endl;
#endif
    char del_char = '0';
    long old_offset = lseek(fd, 0, SEEK_CUR);
    if (old_offset == -1)
        err_exit("lseek 1 delete_from_file_locations", logfile);
    if (lseek(fd, offset, SEEK_SET) == -1)
        err_exit("lseek 2 delete_from_file_locations", logfile);
    write(fd, &del_char, sizeof(char));
    if (lseek(fd, old_offset, SEEK_SET) == -1)
        err_exit("lseek 3 delete_from_file_locations", logfile);
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        logfile << "reader " << my_rank << " failed to unlock the file" << std::endl;
    }

}

void delete_from_metadata(std::string path_to_remove, int my_rank) {
    bool found = false;
    int rank = 0, res = -1;
    std::string node = std::get<0>(files_location[path_to_remove]);
    long int offset = std::get<1>(files_location[path_to_remove]);
    if (offset == -1) { //TODO: very inefficient
#ifdef CAPIOLOG
        logfile << "very slow remove" << std::endl;
#endif
        while (!found && rank < n_servers) {
            std::string rank_str = std::to_string(rank);
            res = delete_from_file_locations("files_location_" + rank_str + ".txt", path_to_remove, my_rank);
            found = res == 1;
            ++rank;
        }
    }
    else {

#ifdef CAPIOLOG
        logfile << "fast remove" << std::endl;
#endif

        std::string file_node_name = std::get<0>(files_location[path_to_remove]);
#ifdef CAPIOLOG
        logfile << "fast remove node_name " << file_node_name << std::endl;
#endif
        int rank;
        if (file_node_name == std::string(node_name))
            rank = my_rank;
        else
            rank = nodes_helper_rank[file_node_name];
#ifdef CAPIOLOG
        logfile << "fast remove rank " << rank << std::endl;
#endif
        //fd_files_location_reads[rank]
        delete_from_file_locations("files_location_" + std::to_string(rank) + ".txt", offset, my_rank, rank);
    }
}

void delete_file(std::string path, int rank) {
#ifdef CAPIOLOG
    logfile << "deleting file " << path << std::endl;
#endif
    sem_wait(&files_metadata_sem);
    files_metadata.erase(path);
    sem_post(&files_metadata_sem);
    delete_from_metadata(path, rank);
    files_location.erase(path);
    for (auto& pair : writers) {
        auto& files = pair.second;
        files.erase(path);
    }
}

#endif //CAPIO_UTIL_DELETE_HPP

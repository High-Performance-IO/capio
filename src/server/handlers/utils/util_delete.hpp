#ifndef CAPIO_UTIL_DELETE_HPP
#define CAPIO_UTIL_DELETE_HPP

#include "utils/metadata.hpp"

/*
 * Returns 0 if the file "file_name" does not exists
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
*/

int delete_from_file_locations(const std::string& file_name, const std::string& path_to_remove, int rank) {
    START_LOG(gettid(), "call(%s, %s, %d)", file_name.c_str(), path_to_remove.c_str(), rank);

    FILE * fp;
    char * line = nullptr;
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
    if (fp == nullptr) {
        logfile << "capio server " << rank << " failed to open the location file" << std::endl;
        return 0;
    }
    fd = fileno(fp);
    if (fd == -1)
        ERR_EXIT("fileno delete_from_file_location");
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        close(fd);
        ERR_EXIT("fcntl lock in delete_from_file_location");
    }
    const char* path_to_check_cstr = path_to_remove.c_str();
    bool found = false;
    long byte = 0;
    while (read != -1 && !found) {
        byte = ftell(fp);
        if (byte == -1)
            ERR_EXIT("ftell delete_from_file_location");

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
            ERR_EXIT("fseek delete_from_file_location");
        fwrite(&del_char, sizeof(char), 1, fp);
        res = 1;
    }
    else
        res = 2;
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        ERR_EXIT("fctl unlock in delete_from_file_location");
    }
    if (fclose(fp) == EOF)
        ERR_EXIT("fclose delete_from_file_location");
    return res;

}

void delete_from_file_locations(const std::string& path_metadata, long int offset, int my_rank, std::size_t rank) {
    START_LOG(gettid(), "call(%s, %ld, %d, %ld)", path_metadata.c_str(), offset, my_rank, rank);

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;    /* shared lock for read*/
    lock.l_whence = SEEK_SET; /* base for seek offsets */
    lock.l_start = 0;         /* 1st byte in file */
    lock.l_len = 0;           /* 0 here means 'until EOF' */
    lock.l_pid = getpid();    /* process id */
    int fd;
    //fd = open(file_name.c_str(), O_RDONLY);  /* -1 signals an error */
    if (rank < fd_files_location_reads.size())
        fd = std::get<0>(fd_files_location_reads[rank]);

    else {

        std::string index_str = std::to_string(rank);
        std::string file_name = "files_location_" + std::to_string(rank) + ".txt";
        FILE* fp = fopen(file_name.c_str(), "r+");
        if (fp == nullptr)
            ERR_EXIT("fopen %s delete_from_file_locations", file_name.c_str());
        fd = fileno(fp);
        if (fd == -1)
            ERR_EXIT("fileno delete_from_file_locations");
        fd_files_location_reads.push_back(std::make_tuple(fd, fp, false));
    }
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        ERR_EXIT("fcntl delete_from_file_locations");
    }
    LOG("fast remove offset %ld", offset);
    char del_char = '0';
    long old_offset = lseek(fd, 0, SEEK_CUR);
    if (old_offset == -1)
        ERR_EXIT("lseek 1 delete_from_file_locations");
    if (lseek(fd, offset, SEEK_SET) == -1)
        ERR_EXIT("lseek 2 delete_from_file_locations");
    write(fd, &del_char, sizeof(char));
    if (lseek(fd, old_offset, SEEK_SET) == -1)
        ERR_EXIT("lseek 3 delete_from_file_locations");
    /* Release the lock explicitly. */
    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        logfile << "reader " << my_rank << " failed to unlock the file" << std::endl;
    }

}

void delete_from_metadata(std::string path_to_remove, int my_rank) {
    START_LOG(gettid(), "call(%s, %d)", path_to_remove.c_str(), my_rank);
    bool found = false;
    int rank = 0, res = -1;
    std::string node = std::get<0>(files_location[path_to_remove]);
    long int offset = std::get<1>(files_location[path_to_remove]);
    if (offset == -1) { //TODO: very inefficient
        while (!found && rank < n_servers) {
            std::string rank_str = std::to_string(rank);
            res = delete_from_file_locations("files_location_" + rank_str + ".txt", path_to_remove, my_rank);
            found = res == 1;
            ++rank;
        }
    }else {
        std::string file_node_name = std::get<0>(files_location[path_to_remove]);


        int rank;
        if (file_node_name == std::string(node_name))
            rank = my_rank;
        else
            rank = nodes_helper_rank[file_node_name];

        //fd_files_location_reads[rank]
        delete_from_file_locations("files_location_" + std::to_string(rank) + ".txt", offset, my_rank, rank);
    }

}

void delete_file(const std::string& path, int rank) {
    START_LOG(gettid(), "call(%s, %d)", path.c_str(), rank);

    delete_capio_file(path.c_str());
    delete_from_metadata(path, rank);
    files_location.erase(path);
    for (auto& pair : writers) {
        auto& files = pair.second;
        files.erase(path);
    }
}

#endif //CAPIO_UTIL_DELETE_HPP

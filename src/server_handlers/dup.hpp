#ifndef CAPIO_DUP_HPP
#define CAPIO_DUP_HPP

void handle_dup(const char* str) {
    int tid;
    int old_fd, new_fd;
    sscanf(str, "dupp %d %d %d", &tid, &old_fd, &new_fd);
    processes_files[tid][new_fd] = processes_files[tid][old_fd];
    std:: string path = processes_files_metadata[tid][old_fd];
    processes_files_metadata[tid][new_fd] = path;
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    ++c_file.n_opens;
}

#endif //CAPIO_DUP_HPP

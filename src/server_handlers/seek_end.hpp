#ifndef CAPIO_SEEK_END_HPP
#define CAPIO_SEEK_END_HPP
void handle_seek_end(char* str) {
    int tid, fd;
    sscanf(str, "send %d %d", &tid, &fd);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    off64_t res = c_file.get_file_size();
    response_buffers[tid]->write(&res);
}
#endif //CAPIO_SEEK_END_HPP

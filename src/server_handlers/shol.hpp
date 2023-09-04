#ifndef CAPIO_SHOL_HPP
#define CAPIO_SHOL_HPP
void handle_shol(char* str) {
    int tid, fd;
    size_t offset;
    sscanf(str, "shol %d %d %zu", &tid, &fd, &offset);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    off64_t res = c_file.seek_hole(offset);
    response_buffers[tid]->write(&res);
}
#endif //CAPIO_SHOL_HPP

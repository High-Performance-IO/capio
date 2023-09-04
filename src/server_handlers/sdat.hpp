#ifndef CAPIO_SDAT_HPP
#define CAPIO_SDAT_HPP
void handle_sdat(char* str) {
    int tid, fd;
    size_t offset;
    sscanf(str, "sdat %d %d %zu", &tid, &fd, &offset);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    off64_t res = c_file.seek_data(offset);
    response_buffers[tid]->write(&res);
}
#endif //CAPIO_SDAT_HPP

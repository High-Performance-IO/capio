#ifndef CAPIO_SERVER_HANDLERS_LSEEK_HPP
#define CAPIO_SERVER_HANDLERS_LSEEK_HPP

void handle_lseek(char* str) {
    int tid, fd;
    size_t offset;
    sscanf(str, "seek %d %d %zu", &tid, &fd, &offset);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    off64_t sector_end = c_file.get_sector_end(offset);
    response_buffers[tid]->write(&sector_end);
}

#endif // CAPIO_SERVER_HANDLERS_LSEEK_HPP

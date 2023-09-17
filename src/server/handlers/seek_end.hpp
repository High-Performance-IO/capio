#ifndef CAPIO_SERVER_HANDLERS_SEEK_END_HPP
#define CAPIO_SERVER_HANDLERS_SEEK_END_HPP

#include "stat.hpp"

void handle_seek_end(char* str, int rank) {
    int tid, fd;
    sscanf(str, "send %d %d", &tid, &fd);
    std::string path = processes_files_metadata[tid][fd];
    sem_wait(&files_metadata_sem);
    Capio_file& c_file = *files_metadata[path];
    sem_post(&files_metadata_sem);
    // seek_end here behaves as stat because we want the file size
    reply_stat(tid, path, rank);
}
#endif // CAPIO_SERVER_HANDLERS_SEEK_END_HPP

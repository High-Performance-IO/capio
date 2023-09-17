#ifndef CAPIO_SERVER_HANDLERS_UNLINK_HPP
#define CAPIO_SERVER_HANDLERS_UNLINK_HPP

void handle_unlink(const char* str, int rank) {
    char path[PATH_MAX];
    off64_t res;
    int tid;
#ifdef CAPIOLOG
    logfile << "handle unlink: " << str << std::endl;
#endif
    sscanf(str, "unlk %d %s", &tid, path);
    sem_wait(&files_metadata_sem);
    auto it = files_metadata.find(path);
    if (it != files_metadata.end()) { //TODO: it works only in the local case
        sem_post(&files_metadata_sem);
        res = 0;
        response_buffers[tid]->write(&res);
        Capio_file& c_file = *(it->second);
        --c_file.n_links;
#ifdef CAPIOLOG
        logfile << "capio unlink n links " << c_file.n_links << " n opens " << c_file.n_opens;
#endif
        if (c_file.n_opens == 0 && c_file.n_links <= 0) {
            delete_file(path, rank);

        }
    }
    else {
        sem_post(&files_metadata_sem);
        res = -1;
        response_buffers[tid]->write(&res);
    }
}


#endif // CAPIO_SERVER_HANDLERS_UNLINK_HPP

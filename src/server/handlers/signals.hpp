#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
#ifdef CAPIOLOG
    logfile << "sigterm captured, freeing resources..." << std::endl;
#endif
    //free all the memory used
    if (sem_wait(&files_metadata_sem) == -1)
        err_exit("sem_wait files_metadata_sem in sig_term_handler","sig_term_handler", logfile);
    if (sem_post(&files_metadata_sem) == -1)
        err_exit("sem_post files_metadata_sem in sig_term_handler", "sig_term_handler", logfile);

    std::string offset_shm_name;
    for (auto& p1 : processes_files) {
        for(auto& p2 : p1.second) {
            offset_shm_name = "offset_" + std::to_string(p1.first) +  "_" + std::to_string(p2.first);
            if (shm_unlink(offset_shm_name.c_str()) == -1)
                err_exit("shm_unlink " + offset_shm_name + " in sig_term_handler","sig_term_handler",  logfile);
        }
    }

    std::string sem_write_shm_name;
    for (auto& pair : response_buffers) {
        pair.second->free_shm();
        delete pair.second;
        sem_write_shm_name = "sem_write" + std::to_string(pair.first);
        if (sem_unlink(sem_write_shm_name.c_str()) == -1)
            err_exit("sem_unlink " + sem_write_shm_name + "in sig_term_handler","sig_term_handler",  logfile);
    }

    for (auto& p : data_buffers) {
        p.second.first->free_shm();
        p.second.second->free_shm();
    }

    buf_requests->free_shm(logfile);

#ifdef CAPIOLOG
    logfile << "server terminated" << std::endl;
#endif
    MPI_Finalize();
    exit(0);
}

void catch_sigterm() {
    static struct sigaction sigact;
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_sigaction = sig_term_handler;
    sigact.sa_flags = SA_SIGINFO;
    int res = sigaction(SIGTERM, &sigact, NULL);
    if (res == -1) {
        err_exit("sigaction for SIGTERM","catch_sigterm",  logfile);
    }
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

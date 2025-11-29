#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

inline void handle_exit_group(int tid) {
    START_LOG(gettid(), "call(tid=%d)", tid);

    LOG("retrieving files from writers for process with pid = %d", tid);
    auto files = client_manager->getProducedFiles(tid);
    for (auto &path : files) {

        LOG("Handling file %s", path.c_str());
        if (CapioCLEngine::get().getCommitRule(path) == capiocl::commit_rules::ON_TERMINATION) {
            CapioFile &c_file = get_capio_file(path.c_str());
            if (c_file.is_dir()) {
                LOG("file %s is dir", path.c_str());
                long int n_committed = c_file.n_files_expected;
                if (n_committed <= c_file.n_files) {
                    LOG("Setting file %s to complete", path.c_str());
                    c_file.set_complete();
                }
            } else {
                LOG("Setting file %s to complete", path.c_str());
                c_file.set_complete();
                c_file.commit();
            }
            c_file.close();
        }
    }

    for (auto &fd : get_capio_fds_for_tid(tid)) {
        std::string path = std::string(get_capio_file_path(tid, fd));
        handle_close(tid, fd);
    }
    client_manager->removeClient(tid);
}

void exit_group_handler(const char *const str) {
    int tid;
    sscanf(str, "%d", &tid);
    handle_exit_group(tid);
}

#endif // CAPIO_SERVER_HANDLERS_EXITG_HPP

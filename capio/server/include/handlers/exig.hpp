#ifndef CAPIO_SERVER_HANDLERS_EXITG_HPP
#define CAPIO_SERVER_HANDLERS_EXITG_HPP

#include "storage/manager.hpp"
extern StorageManager *storage_manager;

inline void handle_exit_group(int tid) {
    START_LOG(gettid(), "call(tid=%d)", tid);

    LOG("retrieving files from writers for process with pid = %d", tid);
    auto files = client_manager->getProducedFiles(tid);
    for (auto &path : files) {

        LOG("Handling file %s", path.c_str());
        if (CapioCLEngine::get().getCommitRule(path) == capiocl::commitRules::ON_TERMINATION) {
            CapioFile &c_file = storage_manager->get(path);
            if (c_file.isDirectory()) {
                LOG("file %s is dir", path.c_str());
                if (const long int n_committed = c_file.getDirectoryExpectedFileCount();
                    n_committed <= c_file.getDirectoryContainedFileCount()) {
                    LOG("Setting file %s to complete", path.c_str());
                    c_file.setCommitted();
                }
            } else {
                LOG("Setting file %s to complete", path.c_str());
                c_file.setCommitted();
                c_file.dump();
            }
            c_file.close();
        }
    }

    for (const auto fd : storage_manager->getFileDescriptors(tid)) {
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

#ifndef CAPIO_SERVER_HANDLERS_EXIT_HPP
#define CAPIO_SERVER_HANDLERS_EXIT_HPP

#include "storage/manager.hpp"
#include "utils/common.hpp"
#include "utils/fs_root.hpp"
extern StorageManager *storage_manager;

void handle_exit_group(int tid) {
    START_LOG(gettid(), "call(tid=%d)", tid);

    LOG("retrieving files from writers for process with pid = %d", tid);
    auto files = client_manager->getProducedFiles(tid);
    for (auto &path : files) {

        LOG("Handling file %s", path.c_str());
        if (CapioCLEngine::get().getCommitRule(path) == capiocl::commitRules::ON_TERMINATION) {
            auto c_file_opt = storage_manager->tryGet(path);
            if (!c_file_opt) {
                // The FS variant produces files that are only on the file system. Any other file
                // missing from storage was removed before exiting and has nothing left to commit.
                if (std::error_code error; std::filesystem::exists(FsRoot::real(path), error)) {
                    LOG("File %s is on the file system. Setting it to complete", path.c_str());
                    fs_storage_manager->commit(path);
                } else {
                    LOG("File %s was removed", path.c_str());
                }
                continue;
            }
            CapioFile &c_file = c_file_opt->get();
            if (c_file.isDirectory()) {
                LOG("file %s is dir", path.c_str());
                long int n_committed = c_file.getDirectoryExpectedFileCount();
                if (n_committed <= c_file.getCurrentDirectoryFileCount()) {
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
    // A thread can exit after sending the first parts of a long path but before the request that
    // completes it: throw those parts away
    path_parts.erase(tid);
}

void exit_group_handler(const char *const str) {
    int tid;
    sscanf(str, "%d", &tid);
    handle_exit_group(tid);
}

#endif // CAPIO_SERVER_HANDLERS_EXIT_HPP

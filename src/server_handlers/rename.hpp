#ifndef CAPIO_RENAME_HPP
#define CAPIO_RENAME_HPP
void handle_rename(const char* str, int rank) {
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];
    int tid;
    off64_t res;
    sscanf(str, "rnam %s %s %d", oldpath, newpath, &tid);

#ifdef CAPIOLOG
    logfile << "handling rename " << std::endl;
#endif
    sem_wait(&files_metadata_sem);
    if (files_metadata.find(oldpath) == files_metadata.end())
        res = 1;
    else
        res = 0;

    sem_post(&files_metadata_sem);
    response_buffers[tid]->write(&res);

    if (res == 1) {
        return;
    }

    for (auto& pair : processes_files_metadata) {
        for (auto& pair_2 : pair.second) {
            if (pair_2.second == oldpath) {
                pair_2.second = newpath;
            }
        }
    }

    sem_wait(&files_metadata_sem);
    auto node = files_metadata.extract(oldpath);
    node.key() = newpath;
    files_metadata.insert(std::move(node));

    sem_post(&files_metadata_sem);
    for (auto& pair : writers) {
        auto node = pair.second.extract(oldpath);
        if (!node.empty()) {
            node.key() = newpath;
            pair.second.insert(std::move(node));
        }
    }


    auto node_2 = files_location.extract(oldpath);
    if (!node_2.empty()) {
        node_2.key() = newpath;
        files_location.insert(std::move(node_2));
    }

    //TODO: streaming + renaming?

    write_file_location(rank, newpath, tid);
    //TODO: remove from files_location oldpath
}

#endif //CAPIO_RENAME_HPP

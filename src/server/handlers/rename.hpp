#ifndef CAPIO_SERVER_HANDLERS_RENAME_HPP
#define CAPIO_SERVER_HANDLERS_RENAME_HPP

void handle_rename(int tid, const char* oldpath, const char* newpath, int rank) {
    START_LOG(gettid(), "call(tid=%d, oldpath=%s, newpath=%s, rank=%d)",
              tid, oldpath, newpath, rank);

    /*
    if(is_absolute(oldpath))
        exit(EXIT_FAILURE);
*/ //TODO: add support for absolutes path
    if (!get_capio_file_opt(oldpath)) {
        write_response(tid, 1);
        return;
    }
    rename_capio_file(oldpath, newpath);
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
    delete_from_file_locations("files_location_" + std::to_string(rank) + ".txt", oldpath, rank);
    write_file_location(rank, newpath, tid);
    //respond to client, as rename() should return 0 on success
    write_response(tid, 0);
}

void rename_handler(const char * const str, int rank) {
    char oldpath[PATH_MAX];
    char newpath[PATH_MAX];
    int tid;
    sscanf(str, "%s %s %d", oldpath, newpath, &tid);
    handle_rename(tid, oldpath, newpath, rank);
}

#endif // CAPIO_SERVER_HANDLERS_RENAME_HPP

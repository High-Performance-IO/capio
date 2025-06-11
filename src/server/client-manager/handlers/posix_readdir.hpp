
#ifndef POSIX_READDIR_HPP
#define POSIX_READDIR_HPP

inline void posix_readdir_handler(const char *const str) {
    pid_t pid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &pid, path);
    START_LOG(gettid(), "call(pid=%d, path=%s", pid, path);

    auto metadata_token = file_manager->getMetadataPath(path);
    LOG("sending to pid %ld token path of %s", pid, metadata_token.c_str());

    client_manager->reply_to_client(pid, metadata_token.length());
    storage_service->reply_to_client_raw(pid, metadata_token.c_str(), metadata_token.length());
}

#endif // POSIX_READDIR_HPP

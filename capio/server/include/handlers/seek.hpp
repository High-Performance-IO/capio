#ifndef CAPIO_SERVER_HANDLERS_SEEK_HPP
#define CAPIO_SERVER_HANDLERS_SEEK_HPP

#include "client-manager/client_manager.hpp"
#include "stat.hpp"
#include "storage/storage_service.hpp"

extern ClientManager *client_manager;
extern StorageService *storage_service;

inline void handle_lseek(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    storage_service->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

void handle_seek_data(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = storage_service->getFile(storage_service->getFilePath(tid, fd)).value();
    offset            = c_file.seek_data(offset);
    storage_service->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

inline void handle_seek_end(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    // seek_end here behaves as stat because we want the file size
    reply_stat(tid, storage_service->getFilePath(tid, fd));
}

inline void handle_seek_hole(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = storage_service->getFile(storage_service->getFilePath(tid, fd)).value();
    offset            = c_file.seek_hole(offset);
    storage_service->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

void lseek_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_lseek(tid, fd, offset);
}

void seek_data_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_data(tid, fd, offset);
}

void seek_end_handler(const char *const str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    handle_seek_end(tid, fd);
}

void seek_hole_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_hole(tid, fd, offset);
}

#endif // CAPIO_SERVER_HANDLERS_SEEK_HPP

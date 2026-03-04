#ifndef CAPIO_SERVER_HANDLERS_SEEK_HPP
#define CAPIO_SERVER_HANDLERS_SEEK_HPP

#include "client/manager.hpp"
#include "client/request.hpp"
#include "storage/manager.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

void handle_lseek(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    storage_manager->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

void handle_seek_data(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = storage_manager->get(tid, fd);
    offset            = c_file.seek_data(offset);
    storage_manager->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

void ClientRequestManager::ClientUtilities::handle_seek_end(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    // seek_end here behaves as stat because we want the file size
    ClientRequestManager::ClientUtilities::reply_stat(tid, storage_manager->getPath(tid, fd));
}

void handle_seek_hole(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    CapioFile &c_file = storage_manager->get(tid, fd);
    offset            = c_file.seek_hole(offset);
    storage_manager->setFileOffset(tid, fd, offset);
    client_manager->replyToClient(tid, offset);
}

void ClientRequestManager::MemHandlers::lseek_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_lseek(tid, fd, offset);
}

void ClientRequestManager::MemHandlers::seek_data_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_data(tid, fd, offset);
}

void ClientRequestManager::MemHandlers::seek_end_handler(const char *const str) {
    int tid, fd;
    sscanf(str, "%d %d", &tid, &fd);
    ClientUtilities::handle_seek_end(tid, fd);
}

void ClientRequestManager::MemHandlers::seek_hole_handler(const char *const str) {
    int tid, fd;
    off64_t offset;
    sscanf(str, "%d %d %ld", &tid, &fd, &offset);
    handle_seek_hole(tid, fd, offset);
}

#endif // CAPIO_SERVER_HANDLERS_SEEK_HPP

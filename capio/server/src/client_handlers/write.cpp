#ifndef CAPIO_SERVER_HANDLERS_WRITE_HPP
#define CAPIO_SERVER_HANDLERS_WRITE_HPP

#include "client/manager.hpp"
#include "client/request.hpp"
#include "storage/manager.hpp"
#include "utils/location.hpp"

extern StorageManager *storage_manager;
extern ClientManager *client_manager;

void ClientRequestManager::MemHandlers::write_handler(const char *const str) {
    std::string request;
    int tid, fd;
    off64_t count;
    sscanf(str, "%d %d %ld", &tid, &fd, &count);

    START_LOG(gettid(), "call(tid=%d, fd=%d, count=%ld)", tid, fd, count);
    // check if another process is waiting for this data
    off64_t offset                    = storage_manager->getFileOffset(tid, fd);
    off64_t end_of_write              = offset + count;
    const std::filesystem::path &path = storage_manager->getPath(tid, fd);
    CapioFile &c_file                 = storage_manager->get(path);
    off64_t file_shm_size             = c_file.get_buf_size();
    SPSCQueue &data_buf               = client_manager->getClientToServerDataBuffers(tid);

    c_file.create_buffer_if_needed(path, true);
    if (end_of_write > file_shm_size) {
        c_file.expand_buffer(end_of_write);
    }
    c_file.read_from_queue(data_buf, offset, count);

    client_manager->registerProducedFile(tid, path);
    c_file.insert_sector(offset, end_of_write);
    if (c_file.first_write) {
        c_file.first_write = false;
        write_file_location(path);
        // TODO: it works only if there is one prod per file
        storage_manager->updateDirectory(tid, path);
    }
    storage_manager->setFileOffset(tid, fd, end_of_write);
}

#endif // CAPIO_SERVER_HANDLERS_WRITE_HPP

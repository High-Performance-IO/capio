#ifndef CAPIO_SERVER_HANDLERS_WRITE_HPP
#define CAPIO_SERVER_HANDLERS_WRITE_HPP

#include "utils/common.hpp"
#include "utils/location.hpp"

inline void write_handler(const char *const str) {
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
    off64_t file_shm_size             = c_file.getBufSize();
    SPSCQueue &data_buf               = client_manager->getClientToServerDataBuffers(tid);

    c_file.createBuffer(path, true);
    if (end_of_write > file_shm_size) {
        c_file.expandBuffer(end_of_write);
    }
    c_file.readFromQueue(data_buf, offset, count);

    register_written_data(tid, path, c_file, offset, end_of_write);
    storage_manager->setFileOffset(tid, fd, end_of_write);
}

#endif // CAPIO_SERVER_HANDLERS_WRITE_HPP

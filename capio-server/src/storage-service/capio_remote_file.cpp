
#include "include/communication-service/data-plane/backend_interface.hpp"
#include "include/storage-service/capio_storage_service.hpp"

#include <include/storage-service/capio_file.hpp>

CapioRemoteFile::CapioRemoteFile(const std::string &filePath, const std::string &home_node)
    : CapioFile(filePath) {}

CapioRemoteFile::~CapioRemoteFile() {}

std::size_t CapioRemoteFile::writeData(const char *buffer, const std::size_t file_offset,
                                       std::size_t buffer_length) {
    return 0;
}

std::size_t CapioRemoteFile::readData(char *buffer, std::size_t file_offset,
                                      std::size_t buffer_size) {
    return 0;
}

void CapioRemoteFile::readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) {}

std::size_t CapioRemoteFile::writeToQueue(SPSCQueue &queue, std::size_t offset,
                                          std::size_t length) const {
    return 0;
}

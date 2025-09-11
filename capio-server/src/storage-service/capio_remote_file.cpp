
#include "include/communication-service/data-plane/backend_interface.hpp"
#include "include/storage-service/capio_storage_service.hpp"

#include <include/storage-service/capio_file.hpp>

CapioRemoteFile::CapioRemoteFile(const std::string &filePath, const std::string &home_node)
    : CapioFile(filePath) {}

CapioRemoteFile::~CapioRemoteFile() {}

std::size_t CapioRemoteFile::writeData(const char *buffer, const std::size_t file_offset,
                                       std::size_t buffer_length) {
    throw std::runtime_error("Not implemented: writeData");
}

std::size_t CapioRemoteFile::readData(char *buffer, std::size_t file_offset,
                                      std::size_t buffer_size) {
    throw std::runtime_error("Not implemented: readData");
}

void CapioRemoteFile::readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) {
    throw std::runtime_error("Not implemented: readFromQueue");
}

std::size_t CapioRemoteFile::writeToQueue(SPSCQueue &queue, std::size_t offset,
                                          std::size_t length) const {
    auto buffer = new char[length];

    auto buffer_size =
        capio_backend->fetchFromRemoteHost(this->fileName, this->homeNode, buffer, offset, length);

    queue.write(buffer, buffer_size);

    delete[] buffer;

    return buffer_size;
}

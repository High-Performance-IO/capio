#ifndef CAPIOREMOTEFILE_HPP
#define CAPIOREMOTEFILE_HPP

#include <capio/queue.hpp>

#include "CapioFile.hpp"

class CapioRemoteFile : public CapioFile {
  public:
    explicit CapioRemoteFile(const std::string &filePath) : CapioFile(filePath) {}

    ~CapioRemoteFile() override {}

    /**
     * Write data to a file stored inside the memory
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param file_offset offset internal to the file
     * @param buffer_length Size of the buffer.
     */
    std::size_t writeData(const char *buffer, const std::size_t file_offset,
                          std::size_t buffer_length) override {
        return 0;
    }

    /**
     * Read from Capio File
     * @param buffer Buffer to read to
     * @param file_offset Starting offset of read operation from CapioMemFile
     * @param buffer_size Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    std::size_t readData(char *buffer, std::size_t file_offset, std::size_t buffer_size) {
        return 0;
    }

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) override {}

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param queue
     * @param offset
     * @param length
     * @return
     */
    std::size_t writeToQueue(SPSCQueue &queue, std::size_t offset,
                             std::size_t length) const override {
        return 0;
    }
};

#endif // CAPIOMEMORYFILE_HPP
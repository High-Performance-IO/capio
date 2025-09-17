#ifndef CAPIO_FILE_HPP
#define CAPIO_FILE_HPP

#include <capio/logger.hpp>
#include <capio/queue.hpp>
#include <iostream>
#include <map>
#include <unistd.h>
#include <vector>

class CapioFile {
  protected:
    const std::string fileName, homeNode;
    std::size_t totalSize;

  public:
    explicit CapioFile(const std::string &filePath,
                       const std::string &home_node = capio_global_configuration->node_name)
        : fileName(filePath), homeNode(home_node), totalSize(0) {};
    virtual ~CapioFile() = default;

    virtual bool is_remote() { return false; }

    [[nodiscard]] std::size_t getSize() const {
        START_LOG(gettid(), "call()");
        return totalSize;
    }

    [[nodiscard]] const std::string &getFileName() const { return fileName; }

    /**
     * Write data to a file stored inside the memory
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param file_offset offset internal to the file
     * @param buffer_length Size of the buffer.
     */
    virtual std::size_t writeData(const char *buffer, std::size_t file_offset,
                                  std::size_t buffer_length) = 0;
    /**
     * Read from Capio File
     * @param buffer Buffer to read to
     * @param file_offset Starting offset of read operation from CapioMemFile
     * @param buffer_size Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    virtual std::size_t readData(char *buffer, std::size_t file_offset,
                                 std::size_t buffer_size)    = 0;

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    virtual void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) = 0;

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param queue
     * @param offset
     * @param length
     * @return
     */
    virtual std::size_t writeToQueue(SPSCQueue &queue, std::size_t offset,
                                     std::size_t length) const = 0;
};

class CapioMemoryFile : public CapioFile {
    std::map<std::size_t, std::vector<char>> memoryBlocks;

    // Static file sizes of file pages
    static constexpr u_int32_t _pageSizeMB    = 4;
    static constexpr u_int64_t _pageSizeBytes = _pageSizeMB * 1024 * 1024;

    char *cross_page_buffer_view;

    /**
     * Compute the offsets required to handle write operations onto CapioMemoryFile
     * @param offset Offset from the start of the file, on which the write operation will begin
     * @param length Size of the buffer that will be written into memory
     * @return tuple
     */
    static auto compute_offsets(const std::size_t offset, std::size_t length);

    /**
     * Retrieve a block with memory already reserved at a given offset
     * @param id
     * @return
     */
    std::vector<char> &get_block(u_int64_t id);

  public:
    explicit CapioMemoryFile(const std::string &filePath);

    ~CapioMemoryFile();

    /**
     * Write data to a file stored inside the memory
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param file_offset offset internal to the file
     * @param buffer_length Size of the buffer.
     */
    std::size_t writeData(const char *buffer, const std::size_t file_offset,
                          std::size_t buffer_length) override;

    /**
     * Read from Capio File
     * @param buffer Buffer to read to
     * @param file_offset Starting offset of read operation from CapioMemFile
     * @param buffer_size Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    std::size_t readData(char *buffer, std::size_t file_offset, std::size_t buffer_size);

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) override;

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param queue
     * @param offset
     * @param length
     * @return
     */
    std::size_t writeToQueue(SPSCQueue &queue, std::size_t offset,
                             std::size_t length) const override;
};

class CapioRemoteFile : public CapioFile {
  public:
    explicit CapioRemoteFile(const std::string &filePath, const std::string &home_node);

    ~CapioRemoteFile() override;

    bool is_remote() override { return true; };

    /**
     * Write data to a file stored inside the memory
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param file_offset offset internal to the file
     * @param buffer_length Size of the buffer.
     */
    std::size_t writeData(const char *buffer, const std::size_t file_offset,
                          std::size_t buffer_length) override;

    /**
     * Read from Capio File
     * @param buffer Buffer to read to
     * @param file_offset Starting offset of read operation from CapioMemFile
     * @param buffer_size Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    std::size_t readData(char *buffer, std::size_t file_offset, std::size_t buffer_size);

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) override;

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param queue
     * @param offset
     * @param length
     * @return
     */
    std::size_t writeToQueue(SPSCQueue &queue, std::size_t offset,
                             std::size_t length) const override;
};

#endif // CAPIO_FILE_HPP

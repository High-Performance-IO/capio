#ifndef CAPIOFILE_HPP
#define CAPIOFILE_HPP

class CapioFile {
  protected:
    const std::string fileName;
    std::size_t totalSize;

  public:
    explicit CapioFile(const std::string &filePath) : fileName(filePath), totalSize(0){};
    virtual ~CapioFile() = default;

    [[nodiscard]] std::size_t getSize() const { return totalSize; }
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

#endif // CAPIOFILE_HPP

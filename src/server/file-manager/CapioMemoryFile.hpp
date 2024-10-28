#ifndef CAPIOMEMORYFILE_HPP
#define CAPIOMEMORYFILE_HPP

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include <capio/queue.hpp>

class CapioMemoryFile {
    std::map<std::size_t, std::vector<char>> memoryBlocks;
    const std::string fileName;
    std::size_t totalSize;

    // maps for bits
    static constexpr u_int32_t _pageSizeMB    = 4;
    static constexpr u_int64_t _pageMask      = 0xFFFFF;
    static constexpr u_int64_t _pageSizeBytes = _pageSizeMB * 1024 * 1024;

    /**
     * Compute the offsets required to handle write operations onto CapioMemoryFile
     * @param offset Offset from the start of the file, on which the write operation will begin
     * @param length Size of the buffer that will be written into memory
     * @return
     */
    static auto compute_offsets(const std::size_t offset, std::size_t length) {
        // Compute the offset of the memoryBlocks component.
        // This is done by first obtaining the MB component of the address
        // and then dividing it by the size in megabyte of the address
        const auto map_offset = (offset >> 20) / _pageSizeMB;

        // Compute the first write offset relative to the first block of memory
        const auto write_offset = offset & _pageMask;

        // compute the first write size. if the write operation is bigger than the size of the page
        // in bytes, then we need to perform the first write operation with size equals to the
        // distance between the write offset and the end of the page. otherwise it is possible to
        // use the given length
        const auto first_write_size =
            length > _pageSizeBytes ? _pageSizeBytes - write_offset : length;

        return std::tuple(map_offset, write_offset, first_write_size);
    }

    /**
     * Retrieve a block with memory already reserved at a given offset
     * @param id
     * @return
     */
    std::vector<char> &get_block(u_int64_t id) {
        std::vector<char> &block = memoryBlocks[id];
        block.resize(_pageSizeBytes); // reserve 4MB of space
        return block;
    }

  public:
    explicit CapioMemoryFile(const std::string &filePath) : fileName(filePath), totalSize(0) {}

    /**
     * Write data to a file stored inside the memory
     * @param offset offset internal to the file
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param length Size of the buffer.
     */
    void writeData(const std::size_t offset, const char *buffer, std::size_t length) {

        const auto &[map_offset, write_offset, first_write_size] = compute_offsets(offset, length);

        // Execute first write which could be a smaller size
        auto &block = get_block(map_offset);

        std::copy_n(buffer, first_write_size, block.begin() + write_offset);

        // update remaining bytes to write
        length -= first_write_size;
        size_t map_count = 1; // start from map following the one obtained from the first write

        // Variable to store the read offset of the input buffer
        auto buffer_offset = first_write_size;

        while (length > 0) {
            block = get_block(map_offset + map_count);

            // Compute the actual size of the current write
            const auto write_size = length > _pageSizeBytes ? _pageSizeBytes : length;

            std::copy_n(buffer + buffer_offset, write_size, block.begin());

            buffer_offset += write_size;
            map_count++;
            length -= _pageSizeBytes;
        }

        totalSize = std::max(totalSize, offset + length);
    }

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) {

        const auto &[map_offset, write_offset, first_write_size] = compute_offsets(offset, length);

        auto &block = get_block(map_offset);

        queue.read(block.data() + write_offset, first_write_size);
        // update remaining bytes to write
        length -= first_write_size;
        size_t map_count = 1; // start from map following the one obtained from the first write

        // Variable to store the read offset of the input buffer
        auto buffer_offset = first_write_size;

        while (length > 0) {
            block                 = get_block(map_offset + map_count);
            // Compute the actual size of the current write
            const auto write_size = length > _pageSizeBytes ? _pageSizeBytes : length;

            queue.read(block.data() + buffer_offset, write_size);
            buffer_offset += write_size;
            map_count++;
            length -= _pageSizeBytes;
        }

        totalSize = std::max(totalSize, offset + length);
    }

    /**
     * Read from Capio File
     * @param offset Starting offset of read operation from CapioMemFile
     * @param buffer Buffer to read to
     * @param length Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    std::size_t readData(std::size_t offset, char *buffer, std::size_t length) const {
        std::size_t bytesRead = 0;

        // Traverse the memory blocks to read the requested data
        for (const auto &[blockOffset, block] : memoryBlocks) {
            if (blockOffset >= offset + length) {
                break; // Past the requested range
            }
            if (blockOffset + block.size() <= offset) {
                continue; // Before the requested range
            }

            // Calculate the start and end points for the read
            std::size_t start = std::max(offset, blockOffset);
            std::size_t end   = std::min(offset + length, blockOffset + block.size());

            // Copy the data to the buffer
            std::size_t copyLength = end - start;
            std::copy(block.begin() + (start - blockOffset),
                      block.begin() + (start - blockOffset) + copyLength,
                      buffer + (start - offset));
            bytesRead += copyLength;
        }

        return bytesRead;
    }

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param offset
     * @param queue
     * @param length
     * @return
     */
    std::size_t writeToQueue(std::size_t offset, SPSCQueue *queue, std::size_t length) const {
        std::size_t bytesRead = 0;

        // Traverse the memory blocks to read the requested data
        for (const auto &[blockOffset, block] : memoryBlocks) {
            if (blockOffset >= offset + length) {
                break; // Past the requested range
            }
            if (blockOffset + block.size() <= offset) {
                continue; // Before the requested range
            }

            // Calculate the start and end points for the read
            std::size_t start = std::max(offset, blockOffset);
            std::size_t end   = std::min(offset + length, blockOffset + block.size());

            // Copy the data to the buffer
            std::size_t copyLength = end - start;
            queue->write(block.data() + (start - blockOffset), copyLength);

            bytesRead += copyLength;
        }

        return bytesRead;
    }

    [[nodiscard]] std::size_t getSize() const { return totalSize; }
    [[nodiscard]] const std::string &getFileName() const { return fileName; }
};

#endif // CAPIOMEMORYFILE_HPP

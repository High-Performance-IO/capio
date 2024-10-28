#ifndef CAPIOMEMORYFILE_HPP
#define CAPIOMEMORYFILE_HPP

#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include <algorithm>

class CapioMemoryFile {
    std::map<std::size_t, std::vector<char>> memoryBlocks;
    const std::string fileName;
    std::size_t totalSize;

    // maps for bits
    static constexpr u_int32_t _pageSizeMB    = 4;
    static constexpr u_int64_t _pageMask      = 0xFFFFF;
    static constexpr u_int64_t _pageSizeBytes = _pageSizeMB * 1024 * 1024;

  public:
    explicit CapioMemoryFile(const std::string &filePath) : fileName(filePath), totalSize(0) {}

    /**
     * Write data to a file stored inside the memory
     * @param offset offset internal to the file
     * @param buffer buffer to store iside memory (i.e. content of the file)
     * @param length Size of the buffer.
     */
    void writeData(const std::size_t offset, const char *buffer, std::size_t length) {

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

        // Execute first write which could be a smaller size
        auto &block = memoryBlocks[map_offset];
        block.resize(_pageSizeBytes); // reserve 4MB of space
        std::copy_n(buffer, first_write_size, block.begin() + write_offset);

        // update remaining bytes to write
        length -= first_write_size;
        size_t map_count = 1; // start from map following the one obtained from the first write

        // Variable to store the read offset of the input buffer
        auto buffer_offset = first_write_size;

        while (length > 0) {
            block = memoryBlocks[map_offset + map_count];
            block.resize(_pageSizeBytes);

            // Compute the actual size of the current write
            const auto write_size = length > _pageSizeBytes ? _pageSizeBytes : length;

            std::copy_n(buffer + buffer_offset, write_size, block.begin());

            buffer_offset += write_size;
            map_count++;
            length -= _pageSizeBytes;
        }

        totalSize = std::max(totalSize, offset + length);
    }

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

    [[nodiscard]] std::size_t getSize() const { return totalSize; }
    [[nodiscard]] const std::string &getFileName() const { return fileName; }
};

#endif // CAPIOMEMORYFILE_HPP

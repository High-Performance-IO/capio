#ifndef CAPIOMEMORYFILE_HPP
#define CAPIOMEMORYFILE_HPP

#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

class CapioMemoryFile {
    std::map<std::size_t, std::vector<char>> memoryBlocks;
    const std::string fileName;
    std::size_t totalSize;

  public:
    explicit CapioMemoryFile(const std::string &filePath) : fileName(filePath), totalSize(0) {}

    void writeData(const std::size_t offset, const char *buffer, const std::size_t length) {

        // Find or create the memory block for the specified offset
        std::vector<char> &block = memoryBlocks[offset];
        block.resize(length); // Resize the block to hold the new data

        // Copy the data into the block
        std::copy(buffer, buffer + length, block.begin());

        // Update the total size if this write extends beyond the current end
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

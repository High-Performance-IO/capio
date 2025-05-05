#ifndef CAPIOMEMORYFILE_HPP
#define CAPIOMEMORYFILE_HPP

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include <capio/queue.hpp>

#include "CapioFile.hpp"

class CapioMemoryFile : public CapioFile {
    std::map<std::size_t, std::vector<char>> memoryBlocks;

    // Static file sizes of file pages
    static constexpr u_int32_t _pageSizeMB = 4;
    static constexpr u_int64_t _pageSizeBytes = _pageSizeMB * 1024 * 1024;

    char *cross_page_buffer_view;

    /**
     * Compute the offsets required to handle write operations onto CapioMemoryFile
     * @param offset Offset from the start of the file, on which the write operation will begin
     * @param length Size of the buffer that will be written into memory
     * @return tuple
     */
    static auto compute_offsets(const std::size_t offset, std::size_t length) {

        START_LOG(gettid(), "call(offset=%llu, length=%llu)", offset, length);
        // Compute the offset of the memoryBlocks component.
        const auto map_offset = offset / _pageSizeBytes;

        // Compute the first write offset relative to the first block of memory
        const auto mem_block_offset = offset % _pageSizeBytes;

        // compute the first write size. if the write operation is bigger than the size of the page
        // in bytes, then we need to perform the first write operation with size equals to the
        // distance between the write offset and the end of the page. otherwise it is possible to
        // use the given length. The returned offset starts from mem_block_offset
        const auto first_write_size =
            length > _pageSizeBytes - mem_block_offset ? _pageSizeBytes - mem_block_offset : length;

        LOG("Computed offsets. map_offset=%llu, mem_block_offset=%llu, first_write_size=%llu",
            map_offset, mem_block_offset, first_write_size);
        return std::tuple(map_offset, mem_block_offset, first_write_size);
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
    explicit CapioMemoryFile(const std::string &filePath) : CapioFile(filePath) {
        cross_page_buffer_view = new char[_pageSizeBytes];
    }

    ~CapioMemoryFile() override {
        delete[] cross_page_buffer_view;
    }

    /**
     * Write data to a file stored inside the memory
     * @param buffer buffer to store inside memory (i.e. content of the file)
     * @param file_offset offset internal to the file
     * @param buffer_length Size of the buffer.
     */
    std::size_t writeData(const char *buffer, const std::size_t file_offset,
                          std::size_t buffer_length) override {

        const auto &[map_offset, write_offset, first_write_size] =
            compute_offsets(file_offset, buffer_length);

        // Execute first write which could be a smaller size
        auto &block = memoryBlocks[map_offset];
        block.resize(_pageSizeBytes); // reserve 4MB of space

        std::copy(buffer, buffer + first_write_size, block.begin() + write_offset);

        // update remaining bytes to write
        buffer_length -= first_write_size;
        size_t map_count = 1; // start from map following the one obtained from the first write

        // Variable to store the read offset of the input buffer
        auto buffer_offset = first_write_size;

        while (buffer_length > 0) {
            auto &block = memoryBlocks[map_offset + map_count];
            block.resize(_pageSizeBytes); // reserve 4MB of space

            // Compute the actual size of the current write
            const auto write_size = buffer_length > _pageSizeBytes ? _pageSizeBytes : buffer_length;

            std::copy(buffer + buffer_offset, buffer + buffer_offset + write_size, block.data());

            buffer_offset += write_size;
            map_count++;
            buffer_length -= _pageSizeBytes;
        }

        totalSize = std::max(totalSize, buffer_offset);
        return totalSize;
    }

    /**
     * Read from Capio File
     * @param buffer Buffer to read to
     * @param file_offset Starting offset of read operation from CapioMemFile
     * @param buffer_size Length of buffer
     * @return number of bytes read from CapioMemoryFile
     */
    std::size_t readData(char *buffer, std::size_t file_offset, std::size_t buffer_size) {
        std::size_t bytesRead = 0;

        const auto &[map_offset, mem_block_offset_begin, target_buffer_size] =
            compute_offsets(file_offset, buffer_size);

        // Traverse the memory blocks to read the requested data starting from the first block of
        // date
        for (auto it = memoryBlocks.lower_bound(map_offset); it != memoryBlocks.end(); ++it) {
            auto &[blockOffset, block] = *it;

            if (blockOffset * _pageSizeBytes >= file_offset + buffer_size) {
                break; // Past the requested range
            }

            // Copy the data to the buffer
            std::size_t copyLength = target_buffer_size - mem_block_offset_begin;
            std::copy(block.begin() + mem_block_offset_begin,
                      block.begin() + mem_block_offset_begin + target_buffer_size,
                      buffer + bytesRead);

            bytesRead += copyLength;
        }

        return bytesRead;
    }

    /**
     * Store data inside the CapioMemoryFile by reading it from a SPSCQueue object. Behaves just
     * like the writeData method
     * @param queue
     * @param offset
     * @param length
     */
    void readFromQueue(SPSCQueue &queue, std::size_t offset, std::size_t length) override {

        const auto &[map_offset, write_offset, first_write_size] = compute_offsets(offset, length);

        auto remaining_bytes = length;

        auto &block = memoryBlocks[map_offset];
        block.resize(_pageSizeBytes); // reserve 4MB of space

        queue.read(block.data() + write_offset, first_write_size);
        // update remaining bytes to write
        remaining_bytes -= first_write_size;
        size_t map_count = 1; // start from map following the one obtained from the first write

        // Variable to store the read offset of the input buffer
        auto buffer_offset = first_write_size;

        while (remaining_bytes > 0) {
            auto &next_block = memoryBlocks[map_offset + map_count];
            next_block.resize(_pageSizeBytes); // reserve 4MB of space
            // Compute the actual size of the current write
            const auto write_size = length > _pageSizeBytes ? _pageSizeBytes : length;

            queue.read(next_block.data(), write_size);
            buffer_offset += write_size;
            map_count++;
            remaining_bytes -= _pageSizeBytes;
        }

        totalSize = std::max(totalSize, offset + length);
    }

    /**
     * Write the content of the capioFile to a SPSCQueue object
     * @param queue
     * @param offset
     * @param length
     * @return
     */
    std::size_t writeToQueue(SPSCQueue &queue, std::size_t offset,
                             std::size_t length) const override {
        std::size_t bytesRead = 0;

        while (bytesRead < length) {
            const auto [map_offset,mem_block_offset_begin , buffer_view_size] =
                compute_offsets(offset, length);

            if (const auto it = memoryBlocks.lower_bound(map_offset); it != memoryBlocks.end()) {
                auto &[blockOffset, block] = *it;

                if (blockOffset >= offset + length) {
                    return bytesRead; // Past the requested range
                }

                // Copy the data to the buffer
                queue.write(block.data() + mem_block_offset_begin, buffer_view_size);

                bytesRead += buffer_view_size;
            }
        }
        return bytesRead;
    }
};

#endif // CAPIOMEMORYFILE_HPP
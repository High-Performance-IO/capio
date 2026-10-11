#ifndef CAPIO_HEAP_HPP
#define CAPIO_HEAP_HPP

/// @brief Heap allocation strategy: allocates a new[]/delete[] buffer in the process heap.
struct HeapAllocator {
    /**
     * @brief Allocate the buffer in the heap, capped by available memory.
     * @param data    Buffer base (updated).
     * @param size    Buffer logical size (updated).
     * @param requested Desired buffer size.
     * @return True on success.
     */
    bool allocate(char *&data, off64_t &size, const off64_t requested) {
        size = computeAllocableMemQuota(requested);
        data = new char[size];
        return true;
    }

    /** @brief Heap buffers cannot be committed to disk; always a no-op failure. */
    bool commit(off64_t &, off64_t) { return false; }

    /** @brief Release the heap buffer. */
    void freeBuffer(const char *data) { delete[] data; }
};

#endif // CAPIO_HEAP_HPP

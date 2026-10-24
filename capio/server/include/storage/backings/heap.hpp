#ifndef CAPIO_HEAP_HPP
#define CAPIO_HEAP_HPP

/// @brief Heap allocation strategy: allocates a new[]/delete[] buffer in the process heap.
struct HeapBacking {
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

    /**
     * @brief Move the buffer into a new heap block of @p new_size, copying only the sectors.
     * @param data     Buffer base (updated).
     * @param size     Buffer logical size (updated).
     * @param new_size New buffer size.
     * @param sectors  Sparse data regions [start, end) to copy into the new buffer.
     * @return True on success.
     */
    bool grow(char *&data, off64_t &size, const off64_t new_size,
              const std::vector<std::pair<off64_t, off64_t>> &sectors) {
        char *const nb = new char[new_size];
        for (const auto &[lo, hi] : sectors) {
            memcpy(nb + lo, data + lo, hi - lo);
        }
        delete[] data;
        data = nb;
        size = new_size;
        return true;
    }

    /** @brief Heap buffers cannot be committed to disk; always a no-op failure. */
    bool commit(off64_t &, off64_t) { return false; }

    /** @brief Release the heap buffer. */
    void freeBuffer(const char *data) { delete[] data; }
};

#endif // CAPIO_HEAP_HPP

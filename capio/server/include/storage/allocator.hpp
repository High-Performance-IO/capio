#ifndef CAPIO_MEMORY_ALLOCATOR_HPP
#define CAPIO_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>
#include <sys/types.h> // off64_t

/// @brief RAII, quota-aware memory allocator for a CapioFile.
/// @details Backs the bytes with the heap (or mmap for permanent files), caps every
/// (re)allocation with the available-memory checks, and owns all frees. Errors are reported as
/// false/empty so callers keep their own fatal-error policy (ERR_EXIT).
class CapioFileMemoryAllocator {
    char *_data      = nullptr; ///< buffer base, nullptr when not allocated
    off64_t _size    = 0;       ///< logical size exposed to the caller
    int _fd          = -1;      ///< backing fd for mmap-backed buffers, else -1
    off64_t _map_len = 0;       ///< original mmap length, kept so commit-shrink still unmaps fully
    bool _mapped     = false;   ///< true for mmap-backed buffers

  public:
    CapioFileMemoryAllocator() = default;
    ~CapioFileMemoryAllocator();
    CapioFileMemoryAllocator(const CapioFileMemoryAllocator &)            = delete;
    CapioFileMemoryAllocator &operator=(const CapioFileMemoryAllocator &) = delete;

    /**
     * @brief Get the buffer base.
     * @return Pointer to the buffer base, or nullptr when not allocated.
     */
    [[nodiscard]] char *data() const;

    /**
     * @brief Get the logical size of the buffer.
     * @return The logical size handed to the caller.
     */
    [[nodiscard]] off64_t size() const;

    /**
     * @brief Check whether any buffer is currently backing this allocator.
     * @return True while no buffer is backing this allocator.
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Allocate a heap-backed buffer, capped by available memory.
     * @param size The requested buffer size.
     * @return True on success, false if already allocated or allocation failed.
     */
    bool allocateHeap(off64_t size);

    /**
     * @brief Allocate an mmap-backed buffer for permanent files.
     * @param path Backing file path for the mmap allocation.
     * @param size The requested buffer size.
     * @return True on success. On failure rolls back to empty and returns false.
     */
    bool allocateMapped(const std::filesystem::path &path, off64_t size);

    /**
     * @brief Grow the buffer to at least @p data_size, copying existing data to the same offsets.
     * @param data_size Required minimum new buffer size.
     * @param sectors Sparse data regions [start, end) to copy into the new buffer.
     * @return Pointer to the new buffer base.
     */
    char *resize(off64_t data_size, const std::vector<std::pair<off64_t, off64_t>> &sectors);

    /**
     * @brief Shrink a mapped-permanent buffer to its committed size and close its backing fd.
     * @param size The committed size to retain.
     * @return True on success.
     */
    bool commit(off64_t size);

    /** @brief Free all owned memory and resources. */
    void release();
};

#endif // CAPIO_MEMORY_ALLOCATOR_HPP
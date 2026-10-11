#ifndef CAPIO_MEMORY_ALLOCATOR_HPP
#define CAPIO_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <sys/types.h> // off64_t

/// RAII, quota-aware memory allocator for a CapioFile. Backs the bytes with the heap (or mmap for
/// permanent files), caps every (re)allocation with the available-memory checks, and owns all
/// frees. Errors are reported as false/empty so callers keep their own fatal-error policy
/// (ERR_EXIT).
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

    /// Buffer base, or nullptr when not allocated.
    [[nodiscard]] char *data() const;

    /// Logical size handed to the caller.
    [[nodiscard]] off64_t size() const;

    /// True while no buffer is backing this allocator.
    [[nodiscard]] bool empty() const;

    /// Heap-backed allocation, capped by available memory. False if already allocated.
    bool allocateHeap(off64_t size);

    /// mmap-backed allocation for permanent files. Rolls back to empty and returns false on
    /// failure.
    bool allocateMapped(const std::filesystem::path &path, off64_t size);

    /// Grow to at least data_size, copying existing bytes through `copy(dst, src)`. Returns new
    /// base.
    char *resize(off64_t data_size, const std::function<void(char *, const char *)> &copy);

    /// Shrink a mapped-permanent buffer to committed `size` and close the backing fd.
    bool commit(off64_t size);

    /// Free all owned memory and resources (idempotent).
    void release();
};

#endif // CAPIO_MEMORY_ALLOCATOR_HPP
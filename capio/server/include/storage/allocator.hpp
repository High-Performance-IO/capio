#ifndef CAPIO_MEMORY_ALLOCATOR_HPP
#define CAPIO_MEMORY_ALLOCATOR_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/mman.h>
#include <sys/types.h> // off64_t
#include <unistd.h>
#include <utility>
#include <vector>

#include "calf/StlLogger.h"

/// Total allocable bytes for a given requested size, honoring host/cgroup memory limits.
/// Declared before the allocator strategies so their inline methods can use it;
/// defined in storage_allocator.cpp.
off64_t computeAllocableMemQuota(off64_t requested_size);

#include "allocators/heap.hpp"
#include "allocators/mapped.hpp"

/// @brief Common buffer allocator: owns the heap-visible buffer pointer/size and the RAII
/// lifecycle, and forwards every backing-specific operation to the strategy template argument.
/// @tparam Allocator Strategy holding the per-backing allocation state and logic (HeapAllocator
/// or MappedAllocator).
template <class Allocator> class MemoryAllocator {
    char *_data   = nullptr; ///< buffer base, nullptr when not allocated
    off64_t _size = 0;       ///< logical size exposed to the caller
    Allocator allocator_;    ///< backing-specific allocation strategy

  public:
    MemoryAllocator() = default;
    ~MemoryAllocator() { release(); }
    MemoryAllocator(const MemoryAllocator &)            = delete;
    MemoryAllocator &operator=(const MemoryAllocator &) = delete;

    /**
     * @brief Get the buffer base.
     * @return Pointer to the buffer base, or nullptr when not allocated.
     */
    [[nodiscard]] char *data() const { return _data; }

    /**
     * @brief Get the logical size of the buffer.
     * @return The logical size handed to the caller.
     */
    [[nodiscard]] off64_t size() const { return _size; }

    /**
     * @brief Check whether any buffer is currently backing this allocator.
     * @return True while no buffer is backing this allocator.
     */
    [[nodiscard]] bool empty() const { return _data == nullptr; }

    /**
     * @brief Heap-backed allocation.
     * @param size The requested buffer size.
     * @return True on success, false if already allocated.
     */
    bool allocate(off64_t size) { return allocator_.allocate(_data, _size, size); }

    /**
     * @brief mmap-backed allocation of a persistent file.
     * @param path Backing file path for the mmap allocation.
     * @param size The requested buffer size.
     * @return True on success; false on failure or on a backing that does not map files.
     */
    bool allocate(const std::filesystem::path &path, off64_t size) {
        return allocator_.allocate(_data, _size, path, size);
    }

    /**
     * @brief Shrink a mapped-permanent buffer to its committed size and close its backing fd.
     * @param size The committed size to retain.
     * @return True on success; false for backings that cannot commit.
     */
    bool commit(off64_t size) { return allocator_.commit(_size, size); }

    /**
     * @brief Grow the buffer to at least @p data_size, copying existing data to the same offsets.
     * @param data_size Required minimum new buffer size.
     * @param sectors Sparse data regions [start, end) to copy into the new buffer.
     * @return Pointer to the new buffer base.
     */
    char *resize(off64_t data_size, const std::vector<std::pair<off64_t, off64_t>> &sectors) {
        const off64_t new_size = std::max(data_size, computeAllocableMemQuota(_size * 2));
        char *const nb         = new char[new_size];
        for (const auto &[lo, hi] : sectors) {
            memcpy(nb + lo, _data + lo, hi - lo);
        }
        allocator_.freeBuffer(_data);
        _data = nb;
        _size = new_size;
        return _data;
    }

    /** @brief Free all owned memory and resources. */
    void release() {
        allocator_.freeBuffer(_data);
        _data = nullptr;
        _size = 0;
    }
};

#endif // CAPIO_MEMORY_ALLOCATOR_HPP
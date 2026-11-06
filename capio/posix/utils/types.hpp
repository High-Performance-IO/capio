#ifndef CAPIO_POSIX_UTILS_TYPES_HPP
#define CAPIO_POSIX_UTILS_TYPES_HPP

#include <fcntl.h>
#include <unordered_map>
#include <unordered_set>

#include "common/queue.hpp"

typedef std::unordered_map<int, std::tuple<std::shared_ptr<off64_t>, off64_t, int, bool>> CPFiles_t;
typedef std::pair<off64_t, off64_t> CPStatResponse_t;
typedef std::unordered_map<int, std::string> CPFileDescriptors_t;
typedef std::unordered_map<std::string, std::unordered_set<int>> CPFilesPaths_t;

typedef int (*CPHandler_t)(long, long, long, long, long, long, long *);

/**
 * One end of a copy_file_range: the descriptor, what the copy needs to know about it, and the
 * position the next read or write happens at. capio_copy_file_range_end() fills it.
 */
struct CPCopyFileRangeEnd_t {
    int fd                  = -1;      ///< Descriptor of this end of the copy
    loff_t *offset          = nullptr; ///< Offset passed by the caller, or nullptr
    bool is_capio           = false;   ///< Whether CAPIO, rather than the kernel, handles fd
    bool is_open            = false;   ///< Whether fd is an open descriptor
    int flags               = 0;       ///< Status flags and access mode, as F_GETFL returns them
    bool is_dir             = false;   ///< Whether fd is a directory
    bool is_regular         = false;   ///< Whether fd is a regular file
    off64_t size            = 0;       ///< Size of the file when the copy starts
    off64_t position        = 0;       ///< Position of the next read or write
    off64_t saved_fd_offset = 0;       ///< CAPIO fd offset to restore when an offset is given

    /**
     * Check whether fd was opened for reading. The access mode O_ACCMODE, that Linux accepts for
     * descriptors used only with ioctl, allows neither reading nor writing.
     *
     * @return whether fd was opened for reading
     */
    [[nodiscard]] bool isReadable() const {
        const int mode = flags & O_ACCMODE;
        return (flags & O_PATH) == 0 && (mode == O_RDONLY || mode == O_RDWR);
    }

    /**
     * Check whether fd was opened for writing. The access mode O_ACCMODE, that Linux accepts for
     * descriptors used only with ioctl, allows neither reading nor writing.
     *
     * @return whether fd was opened for writing
     */
    [[nodiscard]] bool isWritable() const {
        const int mode = flags & O_ACCMODE;
        return (flags & O_PATH) == 0 && (mode == O_WRONLY || mode == O_RDWR);
    }
};

#endif // CAPIO_POSIX_UTILS_TYPES_HPP

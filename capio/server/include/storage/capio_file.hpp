#ifndef CAPIO_SERVER_CAPIO_FILE_HPP
#define CAPIO_SERVER_CAPIO_FILE_HPP

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/queue.hpp"

/**
 * @class CapioFile
 * @brief Manages file data, sparse sectors, and synchronization for the CAPIO server.
 */
class CapioFile {
    /**
     * @struct compareSectors
     * @brief Comparator for the sectors set, ordering by offset.
     */
    struct compareSectors {
        bool operator()(const std::pair<off64_t, off64_t> &lhs,
                        const std::pair<off64_t, off64_t> &rhs) const;
    };

    char *_buf                 = nullptr; ///< Raw pointer to memory buffer for file content
    off64_t _buf_size          = 0;       ///< Allocated size of _buf
    int _fd                    = -1;      ///< File descriptor for permanent/mmap storage
    int _n_links               = 1;       ///< Number of symbolic links to the file
    long int _n_close_expected = -1;      ///< Target close() operations for commitment
    long int _n_close          = 0;       ///< Current count of close() operations
    int _n_opens               = 0;       ///< Current count of open() operations
    int _n_files               = 0;       ///< Count of dirent64 stored (if directory)
    int _n_files_expected      = -1;      ///< Target dirent64 count (if directory)

    bool _home_node   = false; ///< True if this is the home node
    bool _directory   = false; ///< True if this instance represents a directory
    bool _permanent   = false; ///< True if file persists after server exit
    bool _committed   = false; ///< True if file is finalized
    bool _first_write = true;  ///< True if no data has been written yet

    /// @brief Set of [start, end] pairs representing valid data regions
    std::set<std::pair<off64_t, off64_t>, compareSectors> _sectors;

    off64_t _real_file_size = 0; ///< Total logical size of the file

    /// @brief List of {Thread ID, FD} pairs associated with this file
    std::vector<std::pair<int, int>> _threads_fd;

    mutable std::mutex _mutex;                      ///< Synchronization primitive for thread safety
    mutable std::condition_variable _committed_cv;  ///< Wait for commitment
    mutable std::condition_variable _data_avail_cv; ///< Wait for data at specific offsets

    /**
     * @brief Internal helper to calculate stored size without locking.
     * @return Logical size based on the furthest sector end.
     */
    off64_t _getStoredSize() const;

    /**
     * @brief Reallocates the buffer and copies existing sectors to their correct offsets.
     * @param new_p The pointer to the newly allocated memory.
     * @param old_p The pointer to the old memory buffer.
     */
    void _memcopyCapioFile(char *new_p, char *old_p) const;

  public:
    /** @brief Default constructor. Initializes an empty file. */
    CapioFile();

    /**
     * @brief Explicit constructor for directory-specific initialization.
     * @param directory Whether the file is a directory.
     * @param n_files_expected Expected number of entries.
     * @param permanent Persistence flag.
     * @param init_size Initial buffer allocation size.
     * @param n_close_expected Expected number of close calls.
     */
    CapioFile(bool directory, int n_files_expected, bool permanent, off64_t init_size,
              long int n_close_expected);

    /**
     * @brief Standard constructor for files.
     * @param directory Whether the file is a directory.
     * @param permanent Persistence flag.
     * @param init_size Initial buffer allocation size.
     * @param n_close_expected Expected number of close calls.
     */
    CapioFile(bool directory, bool permanent, off64_t init_size, long int n_close_expected);

    CapioFile(const CapioFile &)            = delete;
    CapioFile &operator=(const CapioFile &) = delete;

    /** @brief Destructor. Cleans up allocated buffers and file descriptors. */
    ~CapioFile();

    /** @return True if the file is committed and read-only. */
    [[nodiscard]] bool isCommitted() const;

    /** @return True if the internal buffer has not yet been allocated. */
    [[nodiscard]] bool bufferToAllocate() const;

    /** @return True if the close count matches expected closes. */
    [[nodiscard]] bool closed() const;

    /** @return True if the file is ready for removal. */
    [[nodiscard]] bool deletable() const;

    /** @return True if this is a directory. */
    [[nodiscard]] bool isDirectory() const;

    /** @return True if no write operations have been performed yet. */
    [[nodiscard]] bool isFirstWrite() const;

    /** @brief Blocks the calling thread until setCommitted() is called. */
    void waitForCommit() const;

    /**
     * @brief Blocks until the requested offset is within a valid sector.
     * @param offset The file offset to wait for.
     */
    void waitForData(long offset) const;

    /**
     * @brief Marks the file as committed and notifies waiting threads.
     * @param commit The new status (defaults to true).
     */
    void setCommitted(bool commit = true);

    /**
     * @brief Maps a Thread ID to a specific File Descriptor.
     * @param tid Thread ID.
     * @param fd File descriptor.
     */
    void addFd(int tid, int fd);

    /**
     * @brief Removes a Thread ID/FD mapping.
     * @param tid Thread ID.
     * @param fd File descriptor.
     */
    void removeFd(int tid, int fd);

    /** @brief Increments the internal open counter. */
    void open();

    /** @brief Increments the _n_close counter, while decrementing the _n_open counter. */
    void close();

    /**
     * @brief Initializes the memory buffer or mmap area.
     * @param path Path to the file.
     * @param home_node Whether this node is the home for the file.
     */
    void createBuffer(const std::filesystem::path &path, bool home_node);

    /**
     * @brief Resizes the internal buffer to accommodate more data.
     * @param data_size Required additional size.
     * @return Pointer to the expanded buffer.
     */
    char *expandBuffer(off64_t data_size);

    /** @brief Dump _buf buffer to the file system. */
    void dump();

    /**
     * @brief Tracks a new data range in the file.
     * @param new_start Starting offset of the data.
     * @param new_end Ending offset of the data.
     */
    void insertSector(off64_t new_start, off64_t new_end);

    /**
     * @brief Fetches data from a remote CAPIO node.
     * @param dest Destination node identifier.
     * @param offset File offset to read from.
     * @param buffer_size Amount of data to fetch.
     */
    void readFromNode(const std::string &dest, off64_t offset, off64_t buffer_size) const;

    /**
     * @brief Transfers data from an SPSC queue into the file buffer.
     * @param queue Source queue.
     * @param offset Destination offset in this file.
     * @param num_bytes Number of bytes to transfer.
     */
    void readFromQueue(SPSCQueue &queue, size_t offset, long int num_bytes) const;

    /** @brief Explicitly sets the total file size. */
    void setRealFileSize(off64_t size);

    /** @brief Marks that at least one write has occurred. */
    void registerFirstWrite();

    /**
     * @brief Increases the count of files contained in this directory.
     * @param count the number to increase the internal counter
     */
    void incrementDirFileCnt(int count = 1);

    /** @return Pointer to the raw memory buffer. */
    char *getBuffer() const;

    /** @return Vector of TID/FD pairs. */
    [[nodiscard]] const std::vector<std::pair<int, int>> &getFds() const;

    /** @return The physical size of the current buffer. */
    [[nodiscard]] off64_t getBufSize() const;

    /** @return The logical size of the file. */
    [[nodiscard]] off64_t getRealFileSize() const;

    /** @return The total size, accounting for holes and metadata. */
    [[nodiscard]] off64_t getFileSize() const;

    /** @return Size of data currently residing on this node. */
    [[nodiscard]] off64_t getStoredSize() const;

    /** @return Count of files currently indexed in this directory. */
    [[nodiscard]] int getDirectoryContainedFileCount() const;

    /** @return Expected total files in this directory. */
    [[nodiscard]] int getDirectoryExpectedFileCount() const;

    /** @return Reference to the internal sector map. */
    [[nodiscard]] const std::set<std::pair<off64_t, off64_t>, compareSectors> &getSectors() const;

    /**
     * @brief Finds the end of the sector containing the offset.
     * @param offset Position to check.
     * @return End offset of the sector, or -1 if in a hole.
     */
    [[nodiscard]] off64_t getSectorEnd(off64_t offset) const;

    /**
     * @brief Finds the next data segment.
     * @param offset Start searching from here.
     * @return Offset of data, or error if beyond end of file.
     */
    off64_t seekData(off64_t offset);

    /**
     * @brief Finds the next hole in the file.
     * @param offset Start searching from here.
     * @return Offset of the hole.
     */
    [[nodiscard]] off64_t seekHole(off64_t offset) const;
};

#endif // CAPIO_SERVER_CAPIO_FILE_HPP
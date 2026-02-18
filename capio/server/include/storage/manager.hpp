#ifndef CAPIO_STORAGE_MANAGER_HPP
#define CAPIO_STORAGE_MANAGER_HPP
#include <filesystem>
#include <mutex>
#include <optional>

#include "utils/capio_file.hpp"
#include "utils/shared_mutex.hpp"

#include <unordered_map>
#include <vector>

/**
 * @brief Manages the in-memory storage of files and their associated
 * file descriptors across multiple threads.
 *
 * The StorageService acts as the central repository for all managed files,
 * storing CapioFile objects indexed by their path, and maintaining a mapping
 * of thread IDs (TIDs) and file descriptors (FDs) to the actual file objects
 * and their respective offsets.
 */
class StorageManager {
    /**
     * @brief Mutex to protect access to _storage internal data structure.
     */
    mutable std::shared_mutex _mutex_storage;

    /**
     * @brief Mutex to protect access to _opened_fd_map internal data structure.
     */
    mutable std::shared_mutex _mutex_opened_fd_map;

    /**
     * @brief The core storage map. Stores CapioFile objects, indexed by their
     * absolute file path (as a string).
     */
    std::unordered_map<std::string, CapioFile> _storage;

    /**
     * @brief Structure representing a single file descriptor entry for a specific thread.
     */
    struct ThreadFileDescriptor {
        CapioFile *_pointer;              /// Pointer to the actual stored file in _storage.
        std::shared_ptr<off64_t> _offset; /// Thread file offset for a given file descriptor
        std::filesystem::path _path;      /// The path of the opened file.
    };

    /**
     * @brief Map that stores the association between file descriptors and CapioFiles for each
     * thread ID. Indexed by: `[thread_id][file_descriptor]`
     */
    mutable std::unordered_map<pid_t, std::unordered_map<int, ThreadFileDescriptor>> _opened_fd_map;

    /**
     * Enum with the kind of entry that is being used.
     * REGULAR_ENTRY = Any kind of entry, like files or directories
     * DOT_ENTRY     = The dot entry of the directory content
     * DOT_DOT_ENTRY = The reference to the parent directory
     */
    typedef enum { REGULAR_ENTRY = 0, DOT_ENTRY = 1, DOT_DOT_ENTRY = 2 } ManagerDirEntryType;

    /**
     * @brief Adds a new directory entry (a file or a directory) to the directory's data buffer.
     *
     * This function manually constructs a `linux_dirent64` structure and appends it
     * to the `CapioFile` object representing the directory. Handled types of entries are dictated
     * accordingly to what the ManagerDirEntryType type allows.
     *
     * @param tid The ID of the thread performing the operation.
     * @param file_path The path of the file/directory being added as an entry.
     * @param dir The path of the directory file where the entry is being added.
     * @param type The type of the entry
     */
    void addDirectoryEntry(int tid, const std::filesystem::path &file_path, const std::string &dir,
                           ManagerDirEntryType type);

    /**
     * Private method to remove file descriptor from a tid without acquiring a lock
     * @param tid
     * @param fd
     */
    void _removeFromTid(pid_t tid, int fd);

    /**
     * Add a new file descriptor to the map of opened file descriptors for threads without obtaining
     * a lock
     * @param tid The thread ID.
     * @param fd The file descriptor number.
     * @param path The path of the file being opened.
     * @param offset The initial file offset (typically 0).
     */
    void _addNewFdToMap(pid_t tid, int fd, const std::filesystem::path &path, off64_t offset);

    /**
     * Register a new file descriptor within the CapioFile object
     * @param tid The thread ID.
     * @param fd The file descriptor number.
     * @param path The path of the file being opened.
     * @param register_open Whether this operation should also call open() on the CapioFile
     * instance. defaults to true
     */
    void _addNewFdToStorage(pid_t tid, int fd, const std::filesystem::path &path,
                            bool register_open);

  public:
    /**
     * @brief Constructs a new StorageService instance.
     *
     * Initializes the server and logs its completion.
     */
    StorageManager();

    /**
     * @brief Destroys the StorageService instance.
     *
     * Iterates through all open file descriptors and removes them, ensuring
     * proper cleanup of resources before destruction.
     */
    ~StorageManager();

    /**
     * @brief Retrieves a reference to a CapioFile object from storage.
     *
     * @param path The path of the file to retrieve.
     * @return An `std::optional` containing an `std::reference_wrapper<CapioFile>`
     * if the file exists, or an empty optional otherwise.
     */
    std::optional<std::reference_wrapper<CapioFile>>
    tryGet(const std::filesystem::path &path) const;

    /**
     * @brief Retrieves a reference to a CapioFile object from storage.
     *
     * This method searches for the file associated with the given path in the
     * internal storage map, accessing it under a lock.
     *
     * @param path The path of the file to retrieve.
     * @return A direct reference to the associated CapioFile object.
     * @throws std::runtime_error If the file associated with the path is not present in storage.
     */
    CapioFile &get(const std::filesystem::path &path) const;

    /**
     * @brief Retrieves a reference to a CapioFile object from storage.
     *
     * This method searches for the file associated with the given thread id and file descriptor
     * @param pid Thread id of the calling process
     * @param fd File descriptor of the file associated
     * @throws std::runtime_error If the file associated with the thread id and pid is not found
     * @return reference to CapioFile instance
     */
    CapioFile &get(pid_t pid, int fd) const;

    /**
     * @brief Gets the path associated with a specific file descriptor and thread ID.
     *
     * @param tid The thread ID.
     * @param fd The file descriptor.
     * @return A constant reference to the file path.
     */
    const std::filesystem::path &getPath(int tid, int fd) const;

    /**
     * @brief Retrieves a list of all file descriptors opened by a specific thread.
     *
     * @param tid The thread ID.
     * @return A vector of file descriptors (integers).
     */
    std::vector<int> getFileDescriptors(int tid) const;

    /**
     * @brief Retrieves a map of all open file descriptors across all tracked threads.
     *
     * @return An unordered map where keys are thread IDs and values are vectors
     * of file descriptors opened by that thread.
     */
    std::unordered_map<pid_t, std::vector<int>> getFileDescriptors() const;

    /**
     * @brief Sets the file offset (seek position) for an opened file descriptor.
     *
     * @param tid The thread ID.
     * @param fd The file descriptor.
     * @param offset The new file offset.
     * @return The updated file offset.
     */
    off64_t setFileOffset(pid_t tid, int fd, off64_t offset) const;

    /**
     * @brief Gets the current file offset (seek position) for an opened file descriptor.
     *
     * @param tid The thread ID.
     * @param fd The file descriptor.
     * @return The current file offset.
     */
    off64_t getFileOffset(pid_t tid, int fd) const;

    /**
     * @brief Renames a file by updating its path in both the main storage map and
     * all associated file descriptor maps.
     *
     * @param oldpath The current path of the file.
     * @param newpath The new path for the file.
     */
    void rename(const std::filesystem::path &oldpath, const std::filesystem::path &newpath);

    // TODO: Remove parameter is_dir and make an explicit add for file and one for directories
    // TODO: Rename this method to addFile
    /**
     * @brief Adds a new CapioFile object to the storage map.
     *
     * @param path The path of the file to add.
     * @param is_dir Flag indicating if the path represents a directory.
     * @param init_size The initial size of the file buffer (if applicable).
     * @return A reference to the newly created or existing CapioFile object.
     */
    CapioFile &add(const std::filesystem::path &path, bool is_dir, size_t init_size);

    /**
     * @brief Duplicates an existing file descriptor, similar to the `dup` system call.
     *
     * The new file descriptor shares the same open file description, including the
     * file offset, with the old descriptor.
     *
     * @param tid The thread ID.
     * @param old_fd The file descriptor to duplicate.
     * @param new_fd The new file descriptor number.
     */
    void dup(pid_t tid, int old_fd, int new_fd);

    /**
     * @brief Clones all opened file descriptors from a parent thread to a child thread.
     *
     * Typically used to handle `fork` or `clone` operations where the child process
     * inherits the parent's file descriptors.
     *
     * @param parent_tid The thread ID of the parent.
     * @param child_tid The thread ID of the child.
     */
    void clone(pid_t parent_tid, pid_t child_tid);

    /**
     * @brief Retrieves a list of all file paths currently managed by the service.
     *
     * @return A vector of `std::filesystem::path` objects.
     */
    std::vector<std::filesystem::path> getPaths() const;

    /**
     * @brief Removes a file from the storage map.
     *
     * Also, closes all associated file descriptors before removing the file.
     *
     * @param path The path of the file to remove.
     */
    void remove(const std::filesystem::path &path);

    /**
     * @brief Removes a single file descriptor for a given thread.
     *
     * Decrements the reference count on the CapioFile object.
     *
     * @param tid The thread ID.
     * @param fd The file descriptor to remove.
     */
    void removeFromTid(pid_t tid, int fd);

    /**
     * @brief Adds a new file descriptor entry for a thread, linking it to a CapioFile object.
     *
     * This function is used when a file is opened. It creates a new `ThreadFileDescriptor`
     * entry and updates the `CapioFile`'s open count.
     *
     * @param tid The thread ID.
     * @param fd The file descriptor number.
     * @param path The path of the file being opened.
     * @param offset The initial file offset (typically 0).
     */
    void addFileToTid(pid_t tid, int fd, const std::filesystem::path &path, off64_t offset);

    /**
     * @brief Adds a new directory to the storage service.
     *
     * This function handles the special initial setup for directories, including
     * creating the "." and ".." entries.
     *
     * @param tid The thread ID performing the operation.
     * @param path The path of the directory to add.
     * @return `0` if the directory was added/updated, `1` if it already exists and no action was
     * taken.
     */
    off64_t addDirectory(pid_t tid, const std::filesystem::path &path);

    /**
     * @brief Updates the parent directory to include an entry for a new file/directory.
     *
     * This is called when a new file or directory is created to ensure the parent
     * directory's content reflects the new entry.
     *
     * @param tid The thread ID.
     * @param file_path The path of the file/directory whose entry is being added to the parent.
     */
    void updateDirectory(pid_t tid, const std::filesystem::path &file_path);
};

#endif // CAPIO_STORAGE_MANAGER_HPP
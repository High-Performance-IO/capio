#ifndef CAPIO_STORAGE_SERVICE_HPP
#define CAPIO_STORAGE_SERVICE_HPP
#include <filesystem>
#include <mutex>
#include <optional>

#include "utils/capio_file.hpp"

#include <unordered_map>
#include <vector>

class StorageService {
    std::mutex _node_storage_mutex;
    std::unordered_map<std::string, CapioFile *> _node_storage;

    std::mutex processes_files_mutex;
    std::unordered_map<int, std::unordered_map<int, std::filesystem::path>>
        processes_files_metadata;
    std::unordered_map<int,
                       std::unordered_map<int, std::pair<CapioFile *, std::shared_ptr<off64_t>>>>
        processes_files;

    void addDirectoryEntry(int tid, const std::filesystem::path &file_path, const std::string &dir,
                           int type);

  public:
    StorageService();
    ~StorageService();

    std::optional<std::reference_wrapper<CapioFile>>
    getCapioFile(const std::filesystem::path &path);
    const std::filesystem::path &getFilePath(int tid, int fd);
    std::vector<int> getFileDescriptors(int tid);
    std::unordered_map<int, std::vector<int>> getFileDescriptors();
    off64_t setFileOffset(int tid, int fd, off64_t offset);
    off64_t getFileOffset(int tid, int fd);
    void renameFile(const std::filesystem::path &oldpath, const std::filesystem::path &newpath);
    void add(const std::filesystem::path &path, CapioFile *c_file);
    CapioFile &add(const std::filesystem::path &path, bool is_dir, size_t init_size);
    void dup(int tid, int old_fd, int new_fd);
    void clone(pid_t parent_tid, pid_t child_tid);
    std::vector<std::filesystem::path> getPaths();
    void remove(const std::filesystem::path &path);
    void removeFromTid(int tid, int fd);
    void addFileToTid(int tid, int fd, const std::filesystem::path &path, off64_t offset);
    off64_t addDirectory(int tid, const std::filesystem::path &path);
    void updateDirectory(int tid, const std::filesystem::path &file_path);
};

#endif // CAPIO_STORAGE_SERVICE_HPP

#ifndef CAPIO_SERVER_UTILS_METADATA_HPP
#define CAPIO_SERVER_UTILS_METADATA_HPP

#include <mutex>
#include <optional>

#include "common/filesystem.hpp"

CSFilesMetadata_t files_metadata;
std::mutex files_metadata_mutex;

std::unordered_map<int, std::unordered_map<int, std::pair<CapioFile *, std::shared_ptr<off64_t>>>>
    processes_files;
CSProcessFileMetadataMap_t processes_files_metadata;
std::mutex processes_files_mutex;

inline std::unordered_map<int, std::vector<int>> get_capio_fds() {
    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    std::unordered_map<int, std::vector<int>> tid_fd_pairs;
    for (auto &process : processes_files) {
        for (auto &file : process.second) {
            tid_fd_pairs[process.first].push_back(file.first);
        }
    }
    return tid_fd_pairs;
}

inline std::vector<int> get_capio_fds_for_tid(int tid) {
    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    std::vector<int> fds;
    auto it = processes_files.find(tid);
    if (it != processes_files.end()) {
        fds.reserve(it->second.size());
        for (auto &file : it->second) {
            fds.push_back(file.first);
        }
    }
    return fds;
}

inline std::optional<std::reference_wrapper<CapioFile>>
get_capio_file_opt(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    auto it = files_metadata.find(path);
    if (it == files_metadata.end()) {
        LOG("File %s was not found in files_metadata. returning empty object", path.c_str());
        return {};
    } else {
        LOG("File found. returning contained item");
        return {*it->second};
    }
}

inline CapioFile &get_capio_file(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    auto c_file_opt = get_capio_file_opt(path);
    if (c_file_opt) {
        return c_file_opt->get();
    } else {
        ERR_EXIT("error file %s not present in CAPIO", path.c_str());
    }
}

inline const std::filesystem::path &get_capio_file_path(int tid, int fd) {
    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    return processes_files_metadata[tid][fd];
}

inline off64_t get_capio_file_offset(int tid, int fd) {
    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    return *std::get<1>(processes_files[tid][fd]);
}

inline void add_capio_file(const std::filesystem::path &path, CapioFile *c_file) {
    START_LOG(gettid(), "call(path=%s, capio_file=%ld)", path.c_str(), c_file);
    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    files_metadata[path] = c_file;
    LOG("Capio file added to files_metadata. capio_file==nullptr? %s",
        c_file == nullptr ? "TRUE" : "FALSE");
}

inline void add_capio_file_to_tid(int tid, int fd, const std::filesystem::path &path,
                                  off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, offset=%ld)", tid, fd, path.c_str(), offset);

    CapioFile &c_file = get_capio_file(path);
    c_file.open();
    c_file.add_fd(tid, fd);

    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    processes_files_metadata[tid][fd] = path;
    processes_files[tid][fd]          = std::make_pair(&c_file, std::make_shared<off64_t>(offset));
}

inline void clone_capio_file(pid_t parent_tid, pid_t child_tid) {
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);

    for (auto &fd : get_capio_fds_for_tid(parent_tid)) {
        add_capio_file_to_tid(child_tid, fd, processes_files_metadata[parent_tid][fd],
                              get_capio_file_offset(parent_tid, fd));
    }
}

CapioFile &create_capio_file(const std::filesystem::path &path, bool is_dir, size_t init_size) {
    START_LOG(gettid(), "call(path=%s, is_dir=%s, init_size=%ld)", path.c_str(),
              is_dir ? "true" : "false", init_size);

    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');

    auto commit_rule   = CapioCLEngine::get().getCommitRule(path);
    auto fire_rule     = CapioCLEngine::get().getFireRule(path);
    auto n_file        = CapioCLEngine::get().getDirectoryFileCount(path);
    auto permanent     = CapioCLEngine::get().isPermanent(path);
    auto n_close_count = CapioCLEngine::get().getCommitCloseCount(path);

    if (n_file > 1) {
        // NODE: This is probably because it needs to be filled even when dealing with directories
        init_size = CAPIO_DEFAULT_DIR_INITIAL_SIZE;
    }

    const auto c_file =
        new CapioFile(commit_rule, is_dir, n_file, permanent, init_size, n_close_count);
    add_capio_file(path, c_file);
    return *c_file;
}

inline void delete_capio_file_from_tid(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    CapioFile &c_file = get_capio_file(processes_files_metadata[tid][fd]);
    c_file.remove_fd(tid, fd);
    processes_files_metadata[tid].erase(fd);
    processes_files[tid].erase(fd);
}

inline void delete_capio_file(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    CapioFile &c_file = get_capio_file(path);
    for (auto &[tid, fd] : c_file.get_fds()) {
        delete_capio_file_from_tid(tid, fd);
    }

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    files_metadata.erase(path);
}

inline std::vector<std::filesystem::path> get_capio_file_paths() {
    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    std::vector<std::filesystem::path> paths(files_metadata.size());
    for (const auto &it : files_metadata) {
        paths.emplace_back(it.first);
    }
    return paths;
}

inline void dup_capio_file(int tid, int old_fd, int new_fd) {
    START_LOG(gettid(), "call(old_fd=%d, new_fd=%d)", old_fd, new_fd);
    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    const std::string &path               = processes_files_metadata[tid][old_fd];
    processes_files_metadata[tid][new_fd] = path;
    processes_files[tid][new_fd]          = processes_files[tid][old_fd];
    CapioFile &c_file                     = get_capio_file(path);
    c_file.open();
    c_file.add_fd(tid, new_fd);
}

inline void rename_capio_file(const std::filesystem::path &oldpath,
                              const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());

    {
        const std::lock_guard<std::mutex> lg(processes_files_mutex);
        for (auto &pair : processes_files_metadata) {
            for (auto &pair_2 : pair.second) {
                if (pair_2.second == oldpath) {
                    pair_2.second = newpath;
                }
            }
        }
    }
    {
        const std::lock_guard<std::mutex> lg(files_metadata_mutex);
        auto node  = files_metadata.extract(oldpath);
        node.key() = newpath;
        files_metadata.insert(std::move(node));
    }
}

inline off64_t set_capio_file_offset(int tid, int fd, off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    return *std::get<1>(processes_files[tid][fd]) = offset;
}
#endif // CAPIO_SERVER_UTILS_METADATA_HPP
#ifndef CAPIO_SERVER_UTILS_METADATA_HPP
#define CAPIO_SERVER_UTILS_METADATA_HPP

#include <mutex>
#include <optional>

#include "capio/filesystem.hpp"

CSFilesMetadata_t files_metadata;
std::mutex files_metadata_mutex;

CSProcessFileMap_t processes_files;
CSProcessFileMetadataMap_t processes_files_metadata;
std::mutex processes_files_mutex;

CSMetadataConfMap_t metadata_conf;
CSMetadataConfGlobs_t metadata_conf_globs;

long int match_globs(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    long int res             = -1;
    size_t i                 = 0;
    size_t max_length_prefix = 0;
    while (i < metadata_conf_globs.size()) {
        std::string prefix_str = std::get<0>(metadata_conf_globs[i]);
        size_t prefix_length   = prefix_str.length();
        if (path.native().compare(0, prefix_length, prefix_str) == 0 &&
            prefix_length > max_length_prefix) {
            res               = i;
            max_length_prefix = prefix_length;
        }
        ++i;
    }
    return res;
}

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

inline std::optional<std::reference_wrapper<Capio_file>>
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

inline Capio_file &get_capio_file(const std::filesystem::path &path) {
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

inline void add_capio_file(const std::filesystem::path &path, Capio_file *c_file) {
    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    files_metadata[path] = c_file;
}

inline void add_capio_file_to_tid(int tid, int fd, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s)", tid, fd, path.c_str());

    Capio_file &c_file = get_capio_file(path);
    c_file.open();
    c_file.add_fd(tid, fd);

    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    off64_t *p_offset = (off64_t *) create_shm(
        "offset_" + std::to_string(tid) + "_" + std::to_string(fd), sizeof(off64_t));
    // TODO: what happens if a process open the same file twice?
    processes_files_metadata[tid][fd] = path;
    processes_files[tid][fd]          = std::make_pair(&c_file, p_offset);
}

inline void clone_capio_file(pid_t parent_tid, pid_t child_tid) {
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);

    for (auto &fd : get_capio_fds_for_tid(parent_tid)) {
        add_capio_file_to_tid(child_tid, fd, processes_files_metadata[parent_tid][fd]);
    }
}

Capio_file &create_capio_file(const std::filesystem::path &path, bool is_dir, size_t init_size) {
    START_LOG(gettid(), "call(path=%s, is_dir=%s, init_size=%ld)", path.c_str(),
              is_dir ? "true" : "false", init_size);

    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');
    Capio_file *c_file;
    auto it = metadata_conf.find(path);
    if (it == metadata_conf.end()) {
        long int pos = match_globs(path);
        if (pos == -1) {
            if (is_dir) {
                init_size = CAPIO_DEFAULT_DIR_INITIAL_SIZE;
            }
            c_file = new Capio_file(is_dir, false, init_size);
            add_capio_file(path, c_file);
            return *c_file;
        } else {
            auto &[glob, committed, mode, app_name, n_files, batch_size, permanent, n_close] =
                metadata_conf_globs[pos];
            if (in_dir(path, glob)) {
                n_files = 0;
            }
            if (n_files > 0) {
                init_size = CAPIO_DEFAULT_DIR_INITIAL_SIZE;
                is_dir    = true;
            }
            metadata_conf[path] =
                std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
            c_file =
                new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, n_close);
            add_capio_file(path, c_file);
            return *c_file;
        }
    } else {
        auto &[committed, mode, app_name, n_files, permanent, n_close] = it->second;
        if (n_files > 0) {
            is_dir    = true;
            init_size = CAPIO_DEFAULT_DIR_INITIAL_SIZE;
        }
        c_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, n_close);
        add_capio_file(path, c_file);
        return *c_file;
    }
}

inline void delete_capio_file_from_tid(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::lock_guard<std::mutex> lg(processes_files_mutex);
    Capio_file &c_file = get_capio_file(processes_files_metadata[tid][fd]);
    c_file.remove_fd(tid, fd);
    processes_files_metadata[tid].erase(fd);
    const std::string offset_shm_name = "offset_" + std::to_string(tid) + "_" + std::to_string(fd);
    if (shm_unlink(offset_shm_name.c_str()) == -1) {
        ERR_EXIT("ERROR: shm_unlink %s", offset_shm_name.c_str());
    }
    processes_files[tid].erase(fd);
}

inline void delete_capio_file(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    Capio_file &c_file = get_capio_file(path);
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
    Capio_file &c_file                    = get_capio_file(path);
    c_file.open();
    c_file.add_fd(tid, new_fd);
}

inline Capio_file &init_capio_file(const std::filesystem::path &path, bool home_node) {
    START_LOG(gettid(), "call(path=%s, home_node=%s)", path.c_str(), home_node ? "true" : "false");

    Capio_file &c_file = get_capio_file(path);
    if (c_file.buf_to_allocate()) {
        c_file.create_buffer(path, home_node);
    }
    return c_file;
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

void update_metadata_conf(std::filesystem::path &path, size_t pos, long int n_files,
                          size_t batch_size, const std::string &committed, const std::string &mode,
                          const std::string &app_name, bool permanent, long int n_close) {
    START_LOG(gettid(),
              "call(path=%s, pos=%ld, n_files=%ld, batch_size=%ld, committed=%s, "
              "mode=%s, app_name=%s, permanent=&s, n_close=%ld)",
              path.c_str(), pos, n_files, batch_size, committed.c_str(), mode.c_str(),
              app_name.c_str(), permanent ? "true" : "false", n_close);

    if (pos == std::string::npos && n_files == -1) {
        metadata_conf[path] =
            std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
    } else {
        std::string prefix_str = path.native().substr(0, pos);
        metadata_conf_globs.emplace_back(prefix_str, committed, mode, app_name, n_files, batch_size,
                                         permanent, n_close);
    }
}

#endif // CAPIO_SERVER_UTILS_METADATA_HPP

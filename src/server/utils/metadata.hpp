#ifndef CAPIO_SERVER_UTILS_METADATA_HPP
#define CAPIO_SERVER_UTILS_METADATA_HPP

#include <mutex>
#include <optional>

#include "capio/semaphore.hpp"

CSFilesMetadata_t files_metadata;
std::mutex files_metadata_mutex;

// path -> (committed, mode, app_name, n_files, bool, n_close)
CSMetadataConfMap_t metadata_conf;
// [(glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), (glob, committed, mode, app_name, n_files, batch_size, permanent, n_close), ...]
CSMetadataConfGlobs_t metadata_conf_globs;

CSProcessFileMetadataMap_t processes_files_metadata;

int fd_files_location;

inline std::optional<std::reference_wrapper<Capio_file>> get_capio_file_opt(const char * const path) {
    START_LOG(gettid(), "path=%s", path);

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    auto it = files_metadata.find(path);
    if (it == files_metadata.end()) {
        return {};
    } else {
        return {*it->second};
    }
}

inline Capio_file& get_capio_file(const char * const path) {
    START_LOG(gettid(), "path=%s", path);

    auto c_file_opt = get_capio_file_opt(path);
    if (c_file_opt) {
        return c_file_opt->get();
    } else {
        ERR_EXIT("error file %s not present in CAPIO", path);
    }
}

inline std::string_view get_capio_file_path(int tid, int fd) {
    return processes_files_metadata[tid][fd];
}

inline void add_capio_file(const std::string& path, Capio_file* c_file) {
    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    files_metadata[path] = c_file;
}

inline void add_capio_file_to_tid(int tid, int fd, const std::string& path) {
    processes_files_metadata[tid][fd] = path;
    Capio_file& c_file = get_capio_file(path.data());
    c_file.add_fd(tid, fd);
}

inline void clone_capio_file(pid_t parent_tid, pid_t child_tid) {
    processes_files_metadata[child_tid] = processes_files_metadata[parent_tid];
    for (auto &it: processes_files_metadata[parent_tid]) {
        Capio_file& c_file = get_capio_file(it.second.c_str());
        c_file.open();
    }
}

Capio_file& create_capio_file(const std::string& path, bool is_dir, size_t init_size) {
    START_LOG(gettid(), "call(path=%s, is_dir=%s, init_size=%ld)", path.c_str(), is_dir? "true" : "false", init_size);

    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');
    Capio_file *c_file;
    auto it = metadata_conf.find(path);
    if (it == metadata_conf.end()) {
        long int pos = match_globs(path, &metadata_conf_globs);
        if (pos == -1) {
            if (is_dir) {
                init_size = DIR_INITIAL_SIZE;
            }
            c_file = new Capio_file(is_dir, false, init_size);
            add_capio_file(path, c_file);
            return *c_file;
        } else {
            auto& [glob, committed, mode, app_name, n_files, batch_size, permanent, n_close] = metadata_conf_globs[pos];
            if (in_dir(path, glob)) {
                n_files = 0;
            }
            if (n_files > 0) {
                init_size = DIR_INITIAL_SIZE;
                is_dir = true;
            }
            metadata_conf[path] = std::make_tuple(committed, mode, app_name, n_files, permanent, n_close);
            c_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, n_close);
            add_capio_file(path, c_file);
            return *c_file;
        }
    } else {
        auto& [committed, mode, app_name, n_files, permanent, n_close] = it->second;
        if (n_files > 0) {
            is_dir = true;
            init_size = DIR_INITIAL_SIZE;
        }
        c_file = new Capio_file(committed, mode, is_dir, n_files, permanent, init_size, n_close);
        add_capio_file(path, c_file);
        return *c_file;
    }
}

inline void delete_capio_file(const char * const path) {
    START_LOG(gettid(), "path=%s", path);

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    files_metadata.erase(path);
}

inline void delete_capio_file_from_tid(int tid, int fd) {
    START_LOG(gettid(), "tid=%d, fd=%d", tid, fd);

    std::string_view path = get_capio_file_path(tid, fd);
    Capio_file& c_file = get_capio_file(path.data());
    c_file.remove_fd(tid, fd);
    processes_files_metadata[tid].erase(fd);
}

inline std::vector<std::string_view> get_capio_file_paths() {
    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    std::vector<std::string_view> paths(files_metadata.size());
    for (const auto& it : files_metadata) {
        paths.emplace_back(it.first);
    }
    return paths;
}

inline void dup_capio_file(int tid, int old_fd, int new_fd) {
    const std::string_view& path = processes_files_metadata[tid][old_fd];
    processes_files_metadata[tid][new_fd] = path;
    Capio_file& c_file = get_capio_file(path.data());
    c_file.open();
}


inline Capio_file& init_capio_file(const char * const path, bool home_node) {
    START_LOG(gettid(), "path=%s, home_node=%s", path, home_node? "true" : "false");

    Capio_file& c_file = get_capio_file(path);
    if(c_file.buf_to_allocate()) {
        c_file.create_buffer(path, home_node);
    }
    return c_file;
}

inline void rename_capio_file(const char * const oldpath, const char * const newpath) {
    START_LOG(gettid(), "oldpath=%s, newpath=%s", oldpath, newpath);

    for (auto& pair : processes_files_metadata)
        for (auto& pair_2 : pair.second)
            if (pair_2.second == oldpath)
                pair_2.second = newpath;

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    auto node = files_metadata.extract(oldpath);
    node.key() = newpath;
    files_metadata.insert(std::move(node));
}

#endif // CAPIO_SERVER_UTILS_METADATA_HPP

#include <dirent.h>
#include <filesystem>
#include <list>
#include <unordered_map>

#include "common/dirent.hpp"
#include "common/filesystem.hpp"
#include "common/logger.hpp"
#include "storage/storage_service.hpp"
#include "utils/capio_file.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/location.hpp"
#include "utils/types.hpp"

extern char *node_name;

void StorageService::addDirectoryEntry(const pid_t tid, const std::filesystem::path &file_path,
                                       const std::string &dir, int type) {

    START_LOG(gettid(), "call(file_path=%s, dir=%s, type=%d)", file_path.c_str(), dir.c_str(),
              type);

    linux_dirent64 ld;
    ld.d_ino = std::hash<std::string>{}(file_path);
    std::filesystem::path file_name;
    if (type == 0) {
        file_name = file_path.filename();
        LOG("FILENAME: %s", file_name.c_str());
    } else if (type == 1) {
        file_name = ".";
    } else {
        file_name = "..";
    }

    strcpy(ld.d_name, file_name.c_str());
    LOG("FILENAME LD: %s", ld.d_name);
    ld.d_reclen = sizeof(linux_dirent64);

    CapioFile &c_file = get(dir);
    c_file.create_buffer_if_needed(dir, true);
    void *file_shm       = c_file.get_buffer();
    off64_t file_size    = c_file.get_stored_size();
    off64_t data_size    = file_size + ld.d_reclen;
    size_t file_shm_size = c_file.get_buf_size();
    ld.d_off             = data_size;

    if (data_size > file_shm_size) {
        file_shm = c_file.expand_buffer(data_size);
    }

    ld.d_type = (c_file.is_dir() ? DT_DIR : DT_REG);

    memcpy((char *) file_shm + file_size, &ld, sizeof(ld));
    const off64_t base_offset = file_size;

    LOG("STORED FILENAME LD: %s",
        reinterpret_cast<linux_dirent64 *>(static_cast<char *>(file_shm) + file_size)->d_name);

    c_file.insert_sector(base_offset, data_size);
    ++c_file.n_files;
    client_manager->registerProducedFile(tid, dir);
    if (c_file.n_files == c_file.n_files_expected) {
        c_file.set_complete();
    }
}
StorageService::StorageService() {
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "StorageServer initialization completed.");
}

StorageService::~StorageService() {
    for (auto &[tid, fds] : getFileDescriptors()) {
        for (const auto &fd : fds) {
            removeFromTid(tid, fd);
        }
    }
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "StorageServer teardown completed.");
}

std::optional<std::reference_wrapper<CapioFile>>
StorageService::tryGet(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    const std::lock_guard lg(_mutex);
    if (const auto it = _storage.find(path); it == _storage.end()) {
        LOG("File %s was not found in files_metadata. returning empty object", path.c_str());
        return {};
    } else {
        LOG("File found. returning contained item");
        return {it->second};
    }
}
CapioFile &StorageService::get(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    std::lock_guard lg(_mutex);
    if (_storage.find(path) == _storage.end()) {
        throw std::runtime_error("File " + path.string() + " was not found in local storage");
    }

    return _storage.at(path);
}
CapioFile &StorageService::get(const pid_t pid, const int fd) {
    START_LOG(gettid(), "call(pid=%d, fd=%d)", pid, fd);
    const std::lock_guard lg(_mutex);

    if (_opened_fd_map[pid].find(fd) == _opened_fd_map[pid].end()) {
        throw std::runtime_error("File descriptor " + std::to_string(fd) +
                                 " is not opened for process with tid " + std::to_string(pid));
    }

    return *_opened_fd_map[pid][fd]._pointer;
}

const std::filesystem::path &StorageService::getPath(const pid_t tid, const int fd) {
    const std::lock_guard lg(_mutex);
    return _opened_fd_map[tid][fd]._path;
}

std::vector<int> StorageService::getFileDescriptors(const pid_t tid) {
    const std::lock_guard lg(_mutex);
    std::vector<int> fds;
    for (const auto &[file_descriptor, _] : _opened_fd_map[tid]) {
        fds.push_back(file_descriptor);
    }
    return fds;
}

std::unordered_map<int, std::vector<int>> StorageService::getFileDescriptors() {
    const std::lock_guard lg(_mutex);
    std::unordered_map<int, std::vector<int>> tid_fd_pairs;
    for (auto &[thread_id, opened_files] : _opened_fd_map) {
        for (auto &[file_descriptor, _] : opened_files) {
            tid_fd_pairs[thread_id].push_back(file_descriptor);
        }
    }
    return tid_fd_pairs;
}

off64_t StorageService::setFileOffset(const pid_t tid, const int fd, const off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld)", tid, fd, offset);

    const std::lock_guard lg(_mutex);
    return *_opened_fd_map[tid][fd]._offset = offset;
}

off64_t StorageService::getFileOffset(int tid, int fd) {
    const std::lock_guard lg(_mutex);
    return *_opened_fd_map[tid][fd]._offset;
}

void StorageService::rename(const std::filesystem::path &oldpath,
                            const std::filesystem::path &newpath) {
    START_LOG(gettid(), "call(oldpath=%s, newpath=%s)", oldpath.c_str(), newpath.c_str());

    const std::lock_guard lg(_mutex);

    for (auto &[thread_id, opened_files] : _opened_fd_map) {
        for (auto &[file_descriptor, capio_file_repr] : opened_files) {
            if (capio_file_repr._path == oldpath) {
                capio_file_repr._path = newpath;
            }
        }
    }

    auto node  = _storage.extract(oldpath);
    node.key() = newpath;
    _storage.insert(std::move(node));
}

CapioFile &StorageService::add(const std::filesystem::path &path, bool is_dir, size_t init_size) {

    START_LOG(gettid(), "call(path=%s, is_dir=%s, init_size=%ld)", path.c_str(),
              is_dir ? "true" : "false", init_size);

    std::string shm_name = path;
    std::replace(shm_name.begin(), shm_name.end(), '/', '_');

    auto n_file        = CapioCLEngine::get().getDirectoryFileCount(path);
    auto permanent     = CapioCLEngine::get().isPermanent(path);
    auto n_close_count = CapioCLEngine::get().getCommitCloseCount(path);

    if (n_file > 1) {
        // NODE: This is probably because it needs to be filled even when dealing with directories
        init_size = CAPIO_DEFAULT_DIR_INITIAL_SIZE;
    }

    const std::lock_guard lg(_mutex);
    _storage.try_emplace(path, is_dir, n_file, permanent, init_size, n_close_count);
    return _storage[path];
}

void StorageService::dup(const pid_t tid, const int old_fd, const int new_fd) {
    START_LOG(gettid(), "call(old_fd=%d, new_fd=%d)", old_fd, new_fd);
    const std::lock_guard lg(_mutex);

    _opened_fd_map[tid][new_fd]._path    = _opened_fd_map[tid][old_fd]._path;
    _opened_fd_map[tid][new_fd]._pointer = _opened_fd_map[tid][old_fd]._pointer;
    _opened_fd_map[tid][new_fd]._offset  = _opened_fd_map[tid][old_fd]._offset;

    _opened_fd_map[tid][new_fd]._pointer->open();
    _opened_fd_map[tid][new_fd]._pointer->add_fd(tid, new_fd);
}

void StorageService::clone(const pid_t parent_tid, const pid_t child_tid) {
    START_LOG(gettid(), "call(parent_tid=%d, child_tid=%d)", parent_tid, child_tid);

    for (auto &fd : getFileDescriptors(parent_tid)) {
        addFileToTid(child_tid, fd, _opened_fd_map[parent_tid][fd]._path,
                     getFileOffset(parent_tid, fd));
    }
}
std::vector<std::filesystem::path> StorageService::getPaths() {
    const std::lock_guard lg(_mutex);
    std::vector<std::filesystem::path> paths(_storage.size());
    for (const auto &[file_path, _] : _storage) {
        paths.emplace_back(file_path);
    }
    return paths;
}
void StorageService::remove(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());

    const CapioFile &c_file = get(path);
    for (auto &[tid, fd] : c_file.get_fds()) {
        removeFromTid(tid, fd);
    }

    const std::lock_guard lg(_mutex);
    _storage.erase(path);
}
void StorageService::removeFromTid(const pid_t tid, const int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);

    const std::lock_guard lg(_mutex);

    if (_opened_fd_map[tid][fd]._path.empty()) {
        LOG("PATH is empty. returning");
        return;
    }

    _opened_fd_map[tid][fd]._pointer->remove_fd(tid, fd);
    _opened_fd_map[tid].erase(fd);
}

void StorageService::addFileToTid(const pid_t tid, const int fd, const std::filesystem::path &path,
                                  off64_t offset) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, offset=%ld)", tid, fd, path.c_str(), offset);

    const std::lock_guard lg(_mutex);

    _storage[path].open();
    _storage[path].add_fd(tid, fd);

    _opened_fd_map[tid][fd]._path    = path;
    _opened_fd_map[tid][fd]._pointer = &_storage[path];
    _opened_fd_map[tid][fd]._offset  = std::make_shared<off64_t>(offset);
}

off64_t StorageService::addDirectory(const pid_t tid, const std::filesystem::path &path) {
    START_LOG(tid, "call(path=%s)", path.c_str());

    if (!get_file_location_opt(path)) {
        CapioFile &c_file = add(path, true, CAPIO_DEFAULT_DIR_INITIAL_SIZE);
        if (c_file.first_write) {
            c_file.first_write = false;
            // TODO: it works only if there is one prod per file
            if (is_capio_dir(path)) {
                add_file_location(path, node_name, -1);
            } else {
                write_file_location(path);
                updateDirectory(tid, path);
            }
            addDirectoryEntry(tid, path, path, 1);
            const std::filesystem::path parent_dir = get_parent_dir_path(path);
            addDirectoryEntry(tid, parent_dir, path, 2);
        }
        return 0;
    }
    return 1;
}

void StorageService::updateDirectory(const pid_t tid, const std::filesystem::path &file_path) {
    START_LOG(gettid(), "call(file_path=%s)", file_path.c_str());
    const std::filesystem::path dir = get_parent_dir_path(file_path);
    if (CapioFile &c_file = get(dir.c_str()); c_file.first_write) {
        c_file.first_write = false;
        write_file_location(dir);
    }
    addDirectoryEntry(tid, file_path, dir, 0);
}

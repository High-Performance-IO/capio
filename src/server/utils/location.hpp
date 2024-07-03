#ifndef CAPIO_SERVER_UTILS_LOCATIONS_HPP
#define CAPIO_SERVER_UTILS_LOCATIONS_HPP

#include <mutex>
#include <thread>

#include "capio/types.hpp"

constexpr char CAPIO_SERVER_FILES_LOCATION_NAME[]     = "files_location_%s.txt";
constexpr char CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR = '#';

std::unordered_map<std::string, std::pair<const char *const, off64_t>> files_location;
std::mutex files_location_mutex;

int files_location_fd;
FILE *files_location_fp;
std::unordered_map<std::string, FILE *> files_location_fps;

class Flock {
  private:
    int _fd;
    short _l_type;
    struct flock _lock;

  public:
    inline explicit Flock(const int fd, const short l_type) : _fd(fd), _l_type(l_type), _lock() {
        START_LOG(gettid(), "call(fd=%d, l_type=%d)", _fd, l_type);

        _lock.l_whence = SEEK_SET;
        _lock.l_start  = 0;
        _lock.l_len    = 0;
        _lock.l_pid    = getpid();
    }

    Flock(const Flock &)            = delete;
    Flock &operator=(const Flock &) = delete;
    inline ~Flock()                 = default;

    inline void lock() {
        START_LOG(gettid(), "call()");

        _lock.l_type = _l_type;
        if (fcntl(_fd, F_SETLKW, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to lock the file with error %d (%s)", errno,
                     strerror(errno));
        }
    }

    inline void unlock() {
        START_LOG(gettid(), "call()");

        _lock.l_type = F_UNLCK;
        if (fcntl(_fd, F_SETLK, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to unlock the file with error %d (%s)", errno,
                     strerror(errno));
        }
    }
};

inline std::string get_file_location_name(const std::string &node) {
    START_LOG(gettid(), "node=%s", node.c_str());

    char file_location_name[HOST_NAME_MAX + 20];
    sprintf(file_location_name, CAPIO_SERVER_FILES_LOCATION_NAME, node.c_str());
    return {file_location_name};
}

inline FILE *get_file_location_descriptor(const std::string &name) {
    START_LOG(gettid(), "name=%s", name.c_str());

    if (files_location_fps.find(name) == files_location_fps.end()) {
        FILE *descriptor;
        if ((descriptor = fopen(name.c_str(), "a+")) == nullptr) {
            ERR_EXIT("Error opening %s file: %d (%s)", name.c_str(), errno, strerror(errno));
        }
        if (lseek(fileno(descriptor), 0, SEEK_SET) == -1) {
            ERR_EXIT("Error during lseek in file %s", name.c_str());
        }
        files_location_fps.emplace(name, descriptor);
    }

    return files_location_fps.at(name);
}

inline std::optional<std::reference_wrapper<std::pair<const char *const, long int>>>
get_file_location_opt(const std::filesystem::path &path) {
    START_LOG(gettid(), "path=%s", path.c_str());

    const std::lock_guard<std::mutex> lg(files_metadata_mutex);
    auto it = files_location.find(path);
    if (it == files_location.end()) {
        LOG("File was not found in files_locations. returning empty object");
        return {};
    } else {
        LOG("File found on node %s", it->second);
        return {it->second};
    }
}

inline std::pair<const char *const, long int> &
get_file_location(const std::filesystem::path &path) {
    START_LOG(gettid(), "path=%s", path.c_str());

    auto file_location_opt = get_file_location_opt(path);
    if (file_location_opt) {
        return file_location_opt->get();
    } else {
        ERR_EXIT("error file location %s not present in CAPIO", path.c_str());
    }
}

inline void add_file_location(const std::filesystem::path &path, const char *const p_node_str,
                              long offset) {
    const std::lock_guard<std::mutex> lg(files_location_mutex);
    files_location.emplace(path, std::make_pair(p_node_str, offset));
}

void erase_from_files_location(const std::filesystem::path &path) {
    const std::lock_guard<std::mutex> lg(files_location_mutex);
    files_location.erase(path);
}

void rename_file_location(const std::filesystem::path &oldpath,
                          const std::filesystem::path &newpath) {
    const std::lock_guard<std::mutex> lg(files_location_mutex);
    auto node_2 = files_location.extract(oldpath);
    if (!node_2.empty()) {
        node_2.key() = newpath;
        files_location.insert(std::move(node_2));
    }
}

/**
 * Loads the location of @path_to_load in the current node cache
 * @param path_to_load
 * @return true if path_to_load is present on any node, false otherwise
 */
bool load_file_location(const std::filesystem::path &path_to_load) {
    START_LOG(gettid(), "call(path_to_load=%s)", path_to_load.c_str());
    bool found = false;
    auto line  = reinterpret_cast<char *>(malloc((PATH_MAX + HOST_NAME_MAX + 10) * sizeof(char)));

    for (auto &node : backend->get_nodes()) {
        const std::string name = get_file_location_name(node);
        FILE *descriptor       = get_file_location_descriptor(name);

        Flock file_lock(fileno(descriptor), F_RDLCK);
        const std::lock_guard<Flock> lg(file_lock);

        if (fseek(descriptor, 0, SEEK_SET) == -1) {
            ERR_EXIT("fseek in load_file_location");
        }
        LOG("%s offset has been moved to the beginning of the file", name.c_str());
        size_t len = 0;

        while (getline(&line, &len, descriptor) != -1) {
            if (line[0] != CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
                std::string_view line_str(line);
                auto separator = line_str.find_first_of(' ');
                const std::filesystem::path path(line_str.substr(0, separator));
                if (path.compare(path_to_load) == 0) {
                    std::string node_str(
                        line_str.substr(separator + 1, line_str.length())); // remove ' '
                    node_str.pop_back();                       // remove \n from node name
                    auto node_c = new char[node.length() + 1]; // do not call delete[] on this
                    strcpy(node_c, node.c_str());
                    LOG("path %s was found on node %s", path_to_load.c_str(), node_c);
                    off64_t offset = lseek(fileno(descriptor), 0, SEEK_CUR);
                    if (offset == -1) {
                        ERR_EXIT("ftell in load_file_location");
                    }
                    add_file_location(path, node_c, offset);
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            break;
        }
    }
    free(line);
    return found;
}

/*
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 */
int delete_from_files_location(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    int result           = 2;
    auto &[node, offset] = get_file_location(path);
    LOG("Remove %s from node %s at offset %ld", path.c_str(), node, offset);
    const std::string name = get_file_location_name(node);
    FILE *descriptor       = get_file_location_descriptor(name);

    Flock file_lock(fileno(descriptor), F_WRLCK);
    const std::lock_guard<Flock> lg(file_lock);

    if (offset == -1) {
        if (fseek(descriptor, 0, SEEK_SET) == -1) {
            ERR_EXIT("fseek in load_file_location");
        }
        LOG("%s offset has been moved to the beginning of the file", name.c_str());
        auto line =
            reinterpret_cast<char *>(malloc((PATH_MAX + HOST_NAME_MAX + 10) * sizeof(char)));
        size_t len = 0;
        offset     = 0;
        while (getline(&line, &len, descriptor) != -1) {
            if (line[0] != CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
                std::string_view line_str(line);
                auto separator = line_str.find_first_of(' ');
                const std::filesystem::path current_path(line_str.substr(0, separator));
                if (path.compare(current_path) == 0) {
                    LOG("Path %s found on %s", current_path.c_str(), name.c_str());
                    result = 1;
                    if (lseek(fileno(descriptor), offset, SEEK_SET) == -1) {
                        ERR_EXIT("fseek delete_from_file_location");
                    }
                    if (write(files_location_fd, &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR,
                              sizeof(char)) != 1) {
                        ERR_EXIT("fwrite unable to invalidate file %s in %s", current_path.c_str(),
                                 name.c_str());
                    }
                    LOG("Path %s has been deleted from %s", current_path.c_str(), name.c_str());
                    break;
                }
            }
            offset += static_cast<off_t>(strlen(line));
        }
    } else {
        result = 1;
        if (lseek(fileno(descriptor), offset, SEEK_SET) == -1) {
            ERR_EXIT("lseek 2 delete_from_files_location");
        }
        LOG("%s offset has been moved to %ld", name.c_str(), offset);
        if (write(fileno(descriptor), &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR, sizeof(char)) != 1) {
            ERR_EXIT("write delete_from_files_location");
        }
        LOG("Path %s has been deleted from %s", path.c_str(), name.c_str());
    }

    // Delete from local data structures
    erase_from_files_location(path);
    for (auto &pair : writers) {
        pair.second.erase(path);
    }

    return result;
}

void loop_load_file_location(const std::filesystem::path &path_to_load) {
    START_LOG(gettid(), "call(path_to_load=%s)", path_to_load.c_str());

    while (!load_file_location(path_to_load)) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void open_files_location() {
    START_LOG(gettid(), "call()");

    std::string file_location_name = get_file_location_name(node_name);
    if ((files_location_fp = fopen(file_location_name.c_str(), "w+")) == nullptr) {
        ERR_EXIT("Error opening %s file: %d (%s)", file_location_name.c_str(), errno,
                 strerror(errno));
    }
    if (chmod(file_location_name.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == -1) {
        ERR_EXIT("Error changing permissions for %s file: %d (%s)", file_location_name.c_str(),
                 errno, strerror(errno));
    }
    if ((files_location_fd = fileno(files_location_fp)) == -1) {
        ERR_EXIT("Error obtaining file descriptor for %s file: %d (%s)", file_location_name.c_str(),
                 errno, strerror(errno));
    }
    files_location_fps.emplace(file_location_name, files_location_fp);
}

void write_file_location(const std::filesystem::path &path_to_write) {
    START_LOG(gettid(), "call(path_to_write=%s)", path_to_write.c_str());

    Flock file_lock(files_location_fd, F_WRLCK);
    const std::lock_guard<Flock> lg(file_lock);

    long offset = lseek(files_location_fd, 0, SEEK_END);
    if (offset == -1) {
        ERR_EXIT("lseek in write_file_location");
    }
    const std::string line = path_to_write.native() + " " + node_name + "\n";
    if (write(files_location_fd, line.c_str(), line.length()) == -1) {
        ERR_EXIT("write in write_file_location");
    }
    add_file_location(path_to_write, node_name, offset);
}

#endif // CAPIO_SERVER_UTILS_LOCATIONS_HPP

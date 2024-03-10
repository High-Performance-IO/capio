#ifndef CAPIO_SERVER_UTILS_LOCATIONS_HPP
#define CAPIO_SERVER_UTILS_LOCATIONS_HPP

#include <mutex>
#include <thread>
#include <optional>

#include "metadata.hpp"
#include "utils/types.hpp"

class CapioFileLocations {
  private:
    // pathname => vector of producers, vector of consumers
    std::unordered_map<std::string, std::tuple<std::vector<std::string>, std::vector<std::string>>>
        _locations;

  public:
    void print(){
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON << "Printing File locations: " << std::endl << std::endl
                  << "|===================|===================|====================|" << std::endl
                  << "| Filename          | Producer step     | Consumer step      |" << std::endl
                  << "|===================|===================|====================|" << std::endl;
        for (auto itm : _locations){
            std::cout << "| " << itm.first << std::setfill(' ') << std::setw(20-itm.first.length()) << "| ";
            for(auto itm2 : std::get<0>(itm.second)){
                std::cout << itm2 << std::setfill(' ') << std::setw(20-itm2.length());
            }
            if(std::get<0>(itm.second).empty()){
                std::cout << std::setfill(' ') << std::setw(20);
            }
            std::cout << " | ";

            for(auto itm2 : std::get<1>(itm.second)){
                std::cout << itm2 << std::setfill(' ') << std::setw(20-itm2.length());;
            }
            if(std::get<1>(itm.second).empty()){
                std::cout << std::setfill(' ') << std::setw(20);
            }
            std::cout << "  |" << std::endl;

        }
        std::cout << "|===================|===================|====================|" << std::endl;
        std::cout << std::endl;
    };

    inline void add(std::string &path, std::vector<std::string> &producers,
                    std::vector<std::string> &consumers) {
        _locations.emplace(path, std::make_tuple(producers, consumers));
    }

    inline void newFile(const std::string &path) {
        if(_locations.find(path) == _locations.end())
            _locations.emplace(path,
                           std::make_tuple(std::vector<std::string>(), std::vector<std::string>()));
    }

    inline void addProducer(const std::string &path, const std::string &producer) {
        std::get<0>(_locations.at(path)).emplace_back(producer);
    }

    inline void addConsumer(const std::string &path, const std::string &consumer) {
        std::get<1>(_locations.at(path)).emplace_back(consumer);
    }

    inline void remove(const std::string &path) { _locations.erase(path); }

    inline auto producers(const std::string &path) { return std::get<0>(_locations.at(path)); }

    inline auto consumers(const std::string &path) { return std::get<1>(_locations.at(path)); }
};

constexpr char CAPIO_SERVER_FILES_LOCATION_NAME[]     = "files_location.txt";
constexpr char CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR = '#';

CSFilesLocationMap_t files_location;
std::mutex files_location_mutex;

int files_location_fd;
FILE *files_location_fp;

class FlockGuard {
  private:
    int _fd;
    struct flock _lock;

  public:
    inline explicit FlockGuard(const int fd, const short l_type) : _fd(fd), _lock() {
        START_LOG(gettid(), "call(fd=%d, l_type=%d)", _fd, l_type);

        _lock.l_type   = l_type;
        _lock.l_whence = SEEK_SET;
        _lock.l_start  = 0;
        _lock.l_len    = 0;
        _lock.l_pid    = getpid();
        if (fcntl(_fd, F_SETLKW, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to lock the file with error %d (%s)", errno,
                     strerror(errno));
        }
    }

    FlockGuard(const FlockGuard &)            = delete;
    FlockGuard &operator=(const FlockGuard &) = delete;

    inline ~FlockGuard() {
        START_LOG(gettid(), "call(fd=%d)", _fd);

        _lock.l_type = F_UNLCK;
        if (fcntl(_fd, F_SETLK, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to unlock the file with error %d (%s)", errno,
                     strerror(errno));
        }
    }
};

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

    auto line = reinterpret_cast<char *>(malloc((PATH_MAX + HOST_NAME_MAX + 10) * sizeof(char)));
    const FlockGuard fg(files_location_fd, F_RDLCK);
    off64_t old_offset = lseek(files_location_fd, 0, SEEK_CUR);
    if (old_offset == -1) {
        ERR_EXIT("lseek 1 delete_from_files_location");
    }
    LOG("Current %s offset is %ld", CAPIO_SERVER_FILES_LOCATION_NAME, old_offset);
    if (fseek(files_location_fp, 0, SEEK_SET) == -1) {
        ERR_EXIT("fseek in load_file_location");
    }
    LOG("%s offset has been moved to the beginning of the file", CAPIO_SERVER_FILES_LOCATION_NAME);
    size_t len = 0;
    bool found = false;
    while (getline(&line, &len, files_location_fp) != -1) {
        if (line[0] == CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
            continue;
        }
        std::string_view line_str(line);
        auto separator = line_str.find_first_of(' ');
        const std::filesystem::path path(line_str.substr(0, separator));
        std::string node(line_str.substr(separator + 1, line_str.length())); // remove ' '
        node.pop_back();                             // remove \n from node name
        auto node_str = new char[node.length() + 1]; // do not call delete[] on this
        strcpy(node_str, node.c_str());
        if (path == path_to_load) {
            LOG("path %s was found on node %s", path_to_load.c_str(), node_str);
            off64_t offset = lseek(files_location_fd, 0, SEEK_CUR);
            if (offset == -1) {
                ERR_EXIT("ftell in load_file_location");
            }
            add_file_location(path, node_str, offset);
            found = true;
            break;
        }
    }
    if (lseek(files_location_fd, old_offset, SEEK_SET) == -1) {
        ERR_EXIT("lseek 3 delete_from_files_location");
    }
    LOG("%s offset has been restored to %ld", CAPIO_SERVER_FILES_LOCATION_NAME, old_offset);
    free(line);
    return found;
}

/*
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 */
int delete_from_files_location(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(%s)", path.c_str());

    auto &[node, offset] = get_file_location(path);
    erase_from_files_location(path);
    for (auto &pair : writers) {
        pair.second.erase(path);
    }
    if (offset == -1) {
        const FlockGuard fg(files_location_fd, F_WRLCK);
        long old_offset = lseek(files_location_fd, 0, SEEK_CUR);
        if (old_offset == -1) {
            ERR_EXIT("lseek 1 delete_from_files_location");
        }
        LOG("Current %s offset is %ld", CAPIO_SERVER_FILES_LOCATION_NAME, old_offset);
        if (fseek(files_location_fp, 0, SEEK_SET) == -1) {
            ERR_EXIT("fseek in load_file_location");
        }
        LOG("%s offset has been moved to the beginning of the file",
            CAPIO_SERVER_FILES_LOCATION_NAME);
        auto line =
            reinterpret_cast<char *>(malloc((PATH_MAX + HOST_NAME_MAX + 10) * sizeof(char)));
        size_t len = 0;
        offset     = old_offset;
        int result = 2;
        while (getline(&line, &len, files_location_fp) != -1) {
            if (line[0] == CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
                continue;
            }
            std::string_view line_str(line);
            auto separator = line_str.find_first_of(' ');
            const std::filesystem::path current_path(line_str.substr(0, separator));
            if (is_prefix(path, current_path)) {
                result = 1;
                LOG("Path %s should be deleted from %s", current_path.c_str(),
                    CAPIO_SERVER_FILES_LOCATION_NAME);
                if (lseek(files_location_fd, offset, SEEK_SET) == -1) {
                    ERR_EXIT("fseek delete_from_file_location");
                }
                if (write(files_location_fd, &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR,
                          sizeof(char)) != 1) {
                    ERR_EXIT("fwrite unable to invalidate file %s in %s", current_path.c_str(),
                             CAPIO_SERVER_FILES_LOCATION_NAME);
                }
                LOG("Path %s has been deleted from %s", current_path.c_str(),
                    CAPIO_SERVER_FILES_LOCATION_NAME);
            }
            offset = lseek(files_location_fd, 0, SEEK_CUR);
        }
        if (lseek(files_location_fd, old_offset, SEEK_SET) == -1) {
            ERR_EXIT("lseek 3 delete_from_files_location");
        }
        LOG("%s offset has been restored to %ld", CAPIO_SERVER_FILES_LOCATION_NAME, old_offset);
        free(line);
        return result;
    } else {
        LOG("fast remove offset %ld", offset);
        const FlockGuard fg(files_location_fd, F_WRLCK);
        long old_offset = lseek(files_location_fd, 0, SEEK_CUR);
        if (old_offset == -1) {
            ERR_EXIT("lseek 1 delete_from_files_location");
        }
        if (lseek(files_location_fd, offset, SEEK_SET) == -1) {
            ERR_EXIT("lseek 2 delete_from_files_location");
        }
        if (write(files_location_fd, &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR, sizeof(char)) == -1) {
            ERR_EXIT("write delete_from_files_location");
        }
        if (lseek(files_location_fd, old_offset, SEEK_SET) == -1) {
            ERR_EXIT("lseek 3 delete_from_files_location");
        }
        return 1;
    }
}

void loop_load_file_location(const std::filesystem::path &path_to_load) {
    START_LOG(gettid(), "call(path_to_load=%s)", path_to_load.c_str());

    while (!load_file_location(path_to_load)) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void open_files_location() {
    START_LOG(gettid(), "call()");

    if ((files_location_fp = fopen(CAPIO_SERVER_FILES_LOCATION_NAME, "w+")) == nullptr) {
        ERR_EXIT("Error opening %s file: %d (%s)", CAPIO_SERVER_FILES_LOCATION_NAME, errno,
                 strerror(errno));
    }
    if (chmod(CAPIO_SERVER_FILES_LOCATION_NAME, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == -1) {
        ERR_EXIT("Error changing permissions for %s file: %d (%s)",
                 CAPIO_SERVER_FILES_LOCATION_NAME, errno, strerror(errno));
    }
    if ((files_location_fd = fileno(files_location_fp)) == -1) {
        ERR_EXIT("Error obtaining file descriptor for %s file: %d (%s)",
                 CAPIO_SERVER_FILES_LOCATION_NAME, errno, strerror(errno));
    }
}

void write_file_location(const std::filesystem::path &path_to_write) {
    START_LOG(gettid(), "call(path_to_write=%s)", path_to_write.c_str());

    const FlockGuard fg(files_location_fd, F_WRLCK);

    long offset = lseek(files_location_fd, 0, SEEK_CUR);
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

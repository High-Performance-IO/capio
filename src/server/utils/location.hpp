#ifndef CAPIO_SERVER_UTILS_LOCATIONS_HPP
#define CAPIO_SERVER_UTILS_LOCATIONS_HPP

#include <mutex>
#include <thread>

#include "utils/types.hpp"

constexpr char CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR = '#';

CSFilesLocationMap_t files_location;
std::mutex files_location_mutex;

int fd_files_location;
CSFDFileLocationReadsVector_t fd_files_location_reads;

class FlockGuard {
  private:
    int _fd;
    struct flock _lock;
    bool _close_file;

  public:
    inline explicit FlockGuard(const int fd, const short l_type, bool close_file)
        : _fd(fd), _lock(), _close_file(close_file) {
        START_LOG(gettid(), "call(fd=%d, l_type=%d, close_file=%s)", _fd, l_type,
                  close_file ? "true" : "false");

        _lock.l_type   = l_type;
        _lock.l_whence = SEEK_SET;
        _lock.l_start  = 0;
        _lock.l_len    = 0;
        _lock.l_pid    = getpid();
        if (fcntl(_fd, F_SETLKW, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to lock the file");
        }
    }

    FlockGuard(const FlockGuard &)            = delete;
    FlockGuard &operator=(const FlockGuard &) = delete;

    inline ~FlockGuard() {
        START_LOG(gettid(), "call(fd=%d)", _fd);

        _lock.l_type = F_UNLCK;
        if (fcntl(_fd, F_SETLK, &_lock) < 0) {
            close(_fd);
            ERR_EXIT("CAPIO server failed to unlock the file");
        }
        if (_close_file) {
            close(_fd);
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
    for (int rank = 0; rank < n_servers; rank++) {
        FILE *fp;
        bool seek_needed;
        size_t len = 0;
        int fd;

        if (rank < fd_files_location_reads.size()) {
            fd          = std::get<0>(fd_files_location_reads[rank]);
            fp          = std::get<1>(fd_files_location_reads[rank]);
            seek_needed = std::get<2>(fd_files_location_reads[rank]);
        } else {
            if ((fp = fopen("files_location.txt", "r+")) == nullptr) {
                LOG("Unable to open file_locations.txt from node with rank %d", rank);
                continue;
            }

            if ((fd = fileno(fp)) == -1) {
                ERR_EXIT(
                    "Unable to get fileno from files_location.txt FILE ptr on node with rank %d",
                    rank);
            }
            seek_needed = false;

            fd_files_location_reads.emplace_back(fd, fp, seek_needed);
        }

        const FlockGuard fg(fd, F_RDLCK, false);

        if (seek_needed && (fseek(fp, ftell(fp), SEEK_SET) == -1)) {
            ERR_EXIT("fseek in load_file_location");
        }

        while (getline(&line, &len, fp) != -1) {

            if (line[0] == CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
                continue;
            }
            std::string_view line_str(line);
            auto separator = line_str.find_first_of(' ');
            const std::filesystem::path path(line_str.substr(0, separator));
            std::string node(line_str.substr(separator + 1, line_str.length())); // remove ' '
            node.pop_back(); // remove \n from node name

            LOG("found [%s]@[%s]", path.c_str(), node.c_str());

            auto node_str = new char[node.length() + 1]; // do not call delete[] on this
            strcpy(node_str, node.c_str());
            long offset = ftell(fp);
            if (offset == -1) {
                ERR_EXIT("ftell in load_file_location");
            }
            add_file_location(path, node_str, offset);
            if (path == path_to_load) {
                free(line);

                LOG("path: %s, was found on node with rank %d", path_to_load.c_str(), rank);
                return true;
            }
        }

        std::get<2>(fd_files_location_reads[rank]) = true;
    }
    free(line);
    LOG("path %s has not been found on remote nodes", path_to_load.c_str());
    return false;
}

void clean_files_location() {
    START_LOG(gettid(), "call()");

    std::filesystem::remove("files_location.txt");
}

/*
 * Returns 0 if the file "file_name" does not exists
 * Returns 1 if the location of the file path_to_check is found
 * Returns 2 otherwise
 *
 */
int delete_from_file_locations(const std::string &file_name,
                               const std::filesystem::path &path_to_remove, int rank) {
    START_LOG(gettid(), "call(%s, %s, %d)", file_name.c_str(), path_to_remove.c_str(), rank);

    std::unique_ptr<char[]> line(new char[PATH_MAX]);

    size_t len   = PATH_MAX;
    ssize_t read = 0;
    FILE *fp     = fopen(file_name.c_str(), "r+");
    if (fp == nullptr) {
        LOG("capio server %d failed to open the location file", rank);
        return 0;
    }
    int fd = fileno(fp);
    if (fd == -1) {
        ERR_EXIT("fileno delete_from_file_location");
    }
    const FlockGuard fg(fd, F_WRLCK, true);
    int found = 0;
    long byte = 0;
    while (read != -1 && !found) {
        byte = ftell(fp);
        if (byte == -1) {
            ERR_EXIT("ftell delete_from_file_location");
        }
        char *line_addr = static_cast<char *>(line.get());
        read            = getline(&line_addr, &len, fp);
        if (read == -1) {
            break;
        }
        if (line[0] == CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR) {
            continue;
        }
        char path[PATH_MAX];
        int i = 0;
        while (line[i] != ' ') {
            path[i] = line[i];
            ++i;
        }
        path[i] = '\0';
        if (strcmp(path, path_to_remove.c_str()) == 0) {
            found++;

            if (lseek(fd, byte, SEEK_SET) == -1) {
                ERR_EXIT("fseek delete_from_file_location");
            }
            if (write(fd, &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR, sizeof(char)) != 1) {
                ERR_EXIT("fwrite unable to invalidate file %s in "
                         "files_location_%d.txt",
                         path_to_remove.c_str(), rank);
            }
        }
    }

    LOG("%d files have been invalidated!", found);

    if (found > 0) {
        return 1;
    } else {
        return 2;
    }
}

void delete_from_file_locations(long offset, std::size_t rank) {
    START_LOG(gettid(), "call(%ld, %ld)", offset, rank);

    int fd;
    if (rank < fd_files_location_reads.size()) {
        fd = std::get<0>(fd_files_location_reads[rank]);
    } else {
        std::string file_name = "files_location.txt";
        FILE *fp              = fopen(file_name.c_str(), "r+");
        if (fp == nullptr) {
            ERR_EXIT("fopen %s delete_from_file_locations", file_name.c_str());
        }
        fd = fileno(fp);
        if (fd == -1) {
            ERR_EXIT("fileno delete_from_file_locations");
        }
        fd_files_location_reads.emplace_back(fd, fp, false);
    }

    FlockGuard fg(fd, F_WRLCK, false);
    LOG("fast remove offset %ld", offset);
    long old_offset = lseek(fd, 0, SEEK_CUR);
    if (old_offset == -1) {
        ERR_EXIT("lseek 1 delete_from_file_locations");
    }
    if (lseek(fd, offset, SEEK_SET) == -1) {
        ERR_EXIT("lseek 2 delete_from_file_locations");
    }
    write(fd, &CAPIO_SERVER_INVALIDATE_FILE_PATH_CHAR, sizeof(char));
    if (lseek(fd, old_offset, SEEK_SET) == -1) {
        ERR_EXIT("lseek 3 delete_from_file_locations");
    }
}

void delete_from_file_locations(const std::filesystem::path &path, int rank) {
    START_LOG(gettid(), "call(%s, %d)", path.c_str(), rank);

    bool found           = false;
    int res              = -1;
    auto &[node, offset] = get_file_location(path);
    if (offset == -1) { // TODO: very inefficient
        int r = 0;
        while (!found && r < n_servers) {
            res   = delete_from_file_locations("files_location.txt", path, rank);
            found = res == 1;
            ++r;
        }
    } else {
        int r = (node == std::string(node_name)) ? rank : nodes_helper_rank[node];
        delete_from_file_locations(offset, r);
    }
    erase_from_files_location(path);
    for (auto &pair : writers) {
        pair.second.erase(path);
    }
}

void loop_load_file_location(const std::filesystem::path &path_to_load) {
    START_LOG(gettid(), "call(path_to_load=%s, rank=%d)", path_to_load.c_str());

    while (!load_file_location(path_to_load)) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void open_files_location(int rank) {
    START_LOG(gettid(), "call(%d)", rank);

    int fd;
    if ((fd = open("files_location.txt", O_WRONLY | O_APPEND | O_CREAT, 0664)) == -1) {
        ERR_EXIT("writer error opening file");
    }
    fd_files_location = fd;
}

void write_file_location(int rank, const std::filesystem::path &path_to_write, int tid) {
    START_LOG(gettid(), "call(rank=%d, path_to_write=%s)", rank, path_to_write.c_str());

    const FlockGuard fg(fd_files_location, F_WRLCK, false);

    long offset = lseek(fd_files_location, 0, SEEK_CUR);
    if (offset == -1) {
        ERR_EXIT("lseek in write_file_location");
    }
    const std::string line = path_to_write.native() + " " + node_name + "\n";
    write(fd_files_location, line.c_str(), line.length());
    add_file_location(path_to_write, node_name, offset);
}

#endif // CAPIO_SERVER_UTILS_LOCATIONS_HPP

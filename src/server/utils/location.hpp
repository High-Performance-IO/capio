#ifndef CAPIO_SERVER_UTILS_LOCATIONS_HPP
#define CAPIO_SERVER_UTILS_LOCATIONS_HPP

#include <mutex>
#include <optional>
#include <thread>

#include "metadata.hpp"
#include "utils/types.hpp"

class CapioFileLocations {
  private:
    std::unordered_map<std::string,                         // path name
                       std::tuple<std::vector<std::string>, // Vector for producers
                                  std::vector<std::string>, // Vector for consumers
                                  std::string,              // commit rule
                                  std::string,              // fire_rule
                                  bool,                     // permanent
                                  bool,                     // exclude
                                  bool, // is_file (if true yes otherwise it is a directory)
                                  int,  // commit on file number
                                  long> // directory file count
                       >
        _locations;

    static inline std::string truncate_last_n(const std::string &str, int n) {
        return str.length() > n ? "[..] " + str.substr(str.length() - n) : str;
    }

  public:
    void print() {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_JSON
                  << "Composition of expected CAPIO FS: " << std::endl
                  << std::endl
                  << "|======|===================|===================|====================|========"
                     "============|============|===========|=========|"
                  << std::endl
                  << "| Kind | Filename          | Producer step     | Consumer step      |  "
                     "Commit Rule       |  Fire Rule | Permanent | Exclude |"
                  << std::endl
                  << "|======|===================|===================|====================|========"
                     "============|============|===========|=========|"
                  << std::endl;
        for (auto itm : _locations) {
            std::string name_trunc = truncate_last_n(itm.first, 12);
            auto kind              = std::get<5>(itm.second) ? "F" : "D";

            std::cout << "|   " << kind << "  "
                      << "| " << name_trunc << std::setfill(' ')
                      << std::setw(20 - name_trunc.length()) << "| ";

            auto producers = std::get<0>(itm.second);
            auto consumers = std::get<1>(itm.second);
            auto rowCount =
                producers.size() > consumers.size() ? producers.size() : consumers.size();

            for (int i = 0; i <= rowCount; i++) {
                std::string prod, cons;
                if (i > 0) {
                    std::cout << "|      |                   | ";
                }

                if (i < producers.size()) {
                    auto prod1 = truncate_last_n(producers.at(i), 12);
                    std::cout << prod1 << std::setfill(' ') << std::setw(20 - prod1.length())
                              << " | ";
                } else {
                    std::cout << std::setfill(' ') << std::setw(20) << " | ";
                }

                if (i < consumers.size()) {
                    auto cons1 = truncate_last_n(consumers.at(i), 12);
                    std::cout << " " << cons1 << std::setfill(' ') << std::setw(20 - cons1.length())
                              << " | ";
                } else {
                    std::cout << std::setfill(' ') << std::setw(21) << " | ";
                }

                if (i == 0) {
                    std::string commit_rule = std::get<2>(itm.second),
                                fire_rule   = std::get<3>(itm.second);
                    bool exclude = std::get<4>(itm.second), permanent = std::get<5>(itm.second);

                    std::cout << " " << commit_rule << std::setfill(' ')
                              << std::setw(20 - commit_rule.length()) << " | " << fire_rule
                              << std::setfill(' ') << std::setw(13 - fire_rule.length()) << " | "
                              << "    " << (permanent ? "YES" : "NO ") << "   |   "
                              << (exclude ? "YES" : "NO ") << "   |" << std::endl;
                } else {
                    std::cout << std::setfill(' ') << std::setw(20) << "|" << std::setfill(' ')
                              << std::setw(13) << "|" << std::setfill(' ') << std::setw(12) << "|"
                              << std::setfill(' ') << std::setw(10) << "|" << std::endl;
                }
            }
            std::cout << "*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
                         "~~~~~~~"
                         "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*"
                      << std::endl;
        }
        std::cout << std::endl;
    };

    inline void add(std::string &path, std::vector<std::string> &producers,
                    std::vector<std::string> &consumers, const std::string &commit_rule,
                    const std::string &fire_rule, bool permanent, bool exclude) {
        _locations.emplace(path, std::make_tuple(producers, consumers, commit_rule, fire_rule,
                                                 permanent, exclude, true, -1, -1));
    }

    inline void newFile(const std::string &path) {
        if (_locations.find(path) == _locations.end()) {
            _locations.emplace(
                path, std::make_tuple(std::vector<std::string>(), std::vector<std::string>(),
                                      CAPIO_FILE_COMMITTED_ON_TERMINATION, CAPIO_FILE_MODE_UPDATE,
                                      false, false, true, -1, -1));
        }
    }

    inline void addProducer(const std::string &path, std::string &producer) {
        producer.erase(remove_if(producer.begin(), producer.end(), isspace), producer.end());
        std::get<0>(_locations.at(path)).emplace_back(producer);
    }

    inline void addConsumer(const std::string &path, std::string &consumer) {
        consumer.erase(remove_if(consumer.begin(), consumer.end(), isspace), consumer.end());
        std::get<1>(_locations.at(path)).emplace_back(consumer);
    }

    inline void setCommitRule(const std::string &path, const std::string &commit_rule) {
        std::get<2>(_locations.at(path)) = commit_rule;
    }

    inline void setFireRule(const std::string &path, const std::string &fire_rule) {
        std::get<3>(_locations.at(path)) = fire_rule;
    }

    inline void setPermanent(const std::string &path, bool value) {
        std::get<5>(_locations.at(path)) = value;
    }

    inline void setExclude(const std::string &path, bool value) {
        std::get<4>(_locations.at(path)) = value;
    }

    inline void setDirectory(const std::string &path) { std::get<5>(_locations.at(path)) = false; }

    inline bool isDirectory(const std::string &path) { return !std::get<5>(_locations.at(path)); }

    inline void setFile(const std::string &path) { std::get<5>(_locations.at(path)) = true; }

    inline bool isFile(const std::string &path) { return std::get<5>(_locations.at(path)); }

    inline void setCommitedNumber(const std::string &path, int num) {
        std::get<6>(_locations.at(path)) = num;
    }

    inline void setDirectoryFileCount(const std::string &path, long num) {
        std::get<7>(_locations.at(path)) = num;
    }

    inline void remove(const std::string &path) { _locations.erase(path); }

    // TODO: return vector
    inline auto producers(const std::string &path) { return std::get<0>(_locations.at(path)); }

    // TODO: return vector
    inline auto consumers(const std::string &path) { return std::get<1>(_locations.at(path)); }

    // TODO: as soon as we migrate locations to use this class, finalize method can be removed
    inline void finalize() {
        for (auto file : _locations) {
            auto name                                    = std::filesystem::path(file.first);
            auto [producers, consumers, commit_rule, fire_rule, permanent, exclude, is_file,
                  commitNumber, expected_dir_file_count] = file.second;
            update_metadata_conf(name, file.first.find("*"), expected_dir_file_count,
                                 -1, // NOTE: can be ignored as it is a useless parameter when
                                     // home_node_policies are implemented
                                 commit_rule, fire_rule, producers[0], permanent, commitNumber);
        }
    }
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

    FlockGuard(const FlockGuard &) = delete;

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

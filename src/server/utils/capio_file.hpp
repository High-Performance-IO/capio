#ifndef CAPIO_SERVER_UTILS_CAPIO_FILE_HPP
#define CAPIO_SERVER_UTILS_CAPIO_FILE_HPP

#include <algorithm>
#include <condition_variable>
#include <set>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "capio/logger.hpp"
#include "capio/spsc_queue.hpp"

#include "remote/backend.hpp"

/*
 * Only the server have all the information
 * A process that only read from a file doesn't have the info on the _sectors
 * A process that writes only have info on the sector that he wrote
 * the file size is in shm because all the processes need this info
 * and it's easy to provide it to them using the shm
 */

struct compare {
    bool operator()(const std::pair<off64_t, off64_t> &lhs,
                    const std::pair<off64_t, off64_t> &rhs) const {
        return (lhs.first < rhs.first);
    }
};

class CapioFile {
  private:
    char *_buf = nullptr; // buffer containing the data
    std::size_t _buf_size;
    std::string_view _committed = CAPIO_FILE_COMMITTED_ON_TERMINATION;
    bool _directory             = false;
    // _fd is useful only when the file is memory-mapped
    int _fd                     = -1;
    bool _home_node             = false;
    std::string_view _mode      = CAPIO_FILE_MODE_UPDATE;
    int _n_links                = 1;
    long int _n_close           = 0;
    long int _n_close_expected  = -1;
    int _n_opens                = 0;
    bool _permanent             = false;
    // _sectors stored in memory of the files (only the home node is forced to
    // be up to date)
    std::set<std::pair<off64_t, off64_t>, compare> _sectors;
    // vector of (tid, fd)
    std::vector<std::pair<int, int>> _threads_fd;
    bool _complete = false; // whether the file is completed / committed

    /*sync variables*/
    mutable std::mutex _mutex;
    mutable std::condition_variable _complete_cv;
    mutable std::condition_variable _data_avail_cv;

    inline off64_t _get_stored_size() const {
        auto it = _sectors.rbegin();
        return (it == _sectors.rend()) ? 0 : it->second;
    }

  public:
    bool first_write          = true;
    long int n_files          = 0;  // useful for directories
    long int n_files_expected = -1; // useful for directories
    /*
     * file size in the home node. In a given moment could not be up-to-date.
     * This member is useful because a node different from the home node
     * could need to known the size of the file but not its content
     */

    std::size_t real_file_size = 0;

    CapioFile()
        : _buf_size(0), _committed(CAPIO_FILE_COMMITTED_ON_TERMINATION), _directory(false),
          _permanent(false) {}

    CapioFile(const std::string_view &committed, const std::string_view &mode, bool directory,
              long int n_files_expected, bool permanent, std::size_t init_size,
              long int n_close_expected)
        : _buf_size(init_size), _committed(committed), _directory(directory), _mode(mode),
          _n_close_expected(n_close_expected), _permanent(permanent),
          n_files_expected(n_files_expected + 2) {}

    CapioFile(bool directory, bool permanent, std::size_t init_size, long int n_close_expected = -1)
        : _buf_size(init_size), _committed(CAPIO_FILE_COMMITTED_ON_TERMINATION),
          _directory(directory), _n_close_expected(n_close_expected), _permanent(permanent) {}

    CapioFile(const CapioFile &) = delete;

    CapioFile &operator=(const CapioFile &) = delete;

    ~CapioFile() {
        START_LOG(gettid(), "call()");
        LOG("Deleting capio_file");

        if (_permanent && _home_node) {
            if (_directory) {
                delete[] _buf;
            } else {
                int res = munmap(_buf, _buf_size);
                if (res == -1) {
                    ERR_EXIT("munmap CapioFile");
                }
            }
        } else {
            delete[] _buf;
        }
    }

    [[nodiscard]] inline bool is_complete() const {
        START_LOG(gettid(), "capio_file is complete? %s", this->_complete ? "true" : "false");
        std::lock_guard<std::mutex> lg(_mutex);
        return this->_complete;
    }

    inline void wait_for_completion() const {
        START_LOG(gettid(), "call()");
        LOG("Thread waiting for file to be committed");
        std::unique_lock<std::mutex> lock(_mutex);
        _complete_cv.wait(lock, [this] { return _complete; });
    }

    inline void wait_for_data(long offset) const {
        START_LOG(gettid(), "call()");
        LOG("Thread waiting for data to be available");
        std::unique_lock<std::mutex> lock(_mutex);
        _data_avail_cv.wait(lock, [offset, this] {
            return (offset <= this->_get_stored_size()) || this->_complete;
        });
    }

    inline void set_complete(bool complete = true) {
        START_LOG(gettid(), "setting capio_file._complete=%s", complete ? "true" : "false");
        std::lock_guard<std::mutex> lg(_mutex);
        if (this->_complete != complete) {
            this->_complete = complete;
            if (this->_complete) {
                _complete_cv.notify_all();
                _data_avail_cv.notify_all();
            }
        }
    }

    inline void add_fd(int tid, int fd) { _threads_fd.emplace_back(tid, fd); }

    [[nodiscard]] inline bool buf_to_allocate() const {
        std::lock_guard<std::mutex> lg(_mutex);
        return _buf == nullptr;
    }

    inline void close() {
        _n_close++;
        _n_opens--;
    }

    void commit() {
        START_LOG(gettid(), "call()");

        if (_permanent && !_directory && _home_node) {
            off64_t size = get_file_size();
            if (ftruncate(_fd, size) == -1) {
                ERR_EXIT("ftruncate commit capio_file");
            }
            _buf_size = size;
            if (::close(_fd) == -1) {
                ERR_EXIT("close commit capio_file");
            }
        }
    }

    /*
     * To be called when a process
     * execute a read or a write syscall
     */
    void create_buffer(const std::filesystem::path &path, bool home_node) {
        START_LOG(gettid(), "call(path=%s, home_node=%s)", path.c_str(),
                  home_node ? "true" : "false");
        std::lock_guard<std::mutex> lock(_mutex);
        // TODO: will use malloc in order to be able to use realloc
        _home_node = home_node;
        if (_permanent && home_node) {
            if (_directory) {
                std::filesystem::create_directory(path);
                std::filesystem::permissions(path, std::filesystem::perms::owner_all);
                _buf = new char[_buf_size];
            } else {
                LOG("creating mem mapped file");
                _fd = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
                if (_fd == -1) {
                    ERR_EXIT("open %s CapioFile constructor", path.c_str());
                }
                if (ftruncate(_fd, _buf_size) == -1) {
                    ERR_EXIT("ftruncate CapioFile constructor");
                }
                _buf =
                    (char *) mmap(nullptr, _buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
                if (_buf == MAP_FAILED) {
                    ERR_EXIT("mmap CapioFile constructor");
                }
            }
        } else {
            _buf = new char[_buf_size];
        }
    }

    inline void create_buffer_if_needed(const std::filesystem::path &path, bool home_node) {
        if (buf_to_allocate()) {
            create_buffer(path, home_node);
        }
    }

    void memcpy_capio_file(char *new_p, char *old_p) const {
        for (auto &sector : _sectors) {
            off64_t lbound        = sector.first;
            off64_t ubound        = sector.second;
            off64_t sector_length = ubound - lbound;
            memcpy(new_p + lbound, old_p + lbound, sector_length);
        }
    }

    char *expand_buffer(std::size_t data_size) { // TODO: use realloc
        size_t double_size = _buf_size * 2;
        size_t new_size    = data_size > double_size ? data_size : double_size;
        char *new_buf      = new char[new_size];
        std::lock_guard<std::mutex> lock(_mutex);
        //	memcpy(new_p, old_p, file_shm_size); //TODO memcpy only the
        // sector
        // stored in CapioFile
        memcpy_capio_file(new_buf, _buf);
        delete[] _buf;
        _buf      = new_buf;
        _buf_size = new_size;
        return new_buf;
    }

    inline char *get_buffer() { return _buf; }

    [[nodiscard]] inline size_t get_buf_size() const { return _buf_size; }

    [[nodiscard]] inline const std::string_view &get_committed() const { return _committed; }

    [[nodiscard]] inline const std::vector<std::pair<int, int>> &get_fds() const {
        return _threads_fd;
    }

    [[nodiscard]] inline off64_t get_file_size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_sectors.empty()) {
            return _sectors.rbegin()->second;
        } else {
            return 0;
        }
    }

    [[nodiscard]] inline const std::string_view &get_mode() const {
        START_LOG(gettid(), "call()");
        return _mode;
    }

    /*
     * Returns the offset to the end of the sector
     * if the offset parameter is inside of the
     * sector, -1 otherwise
     *
     */
    [[nodiscard]] off64_t get_sector_end(off64_t offset) const {
        off64_t sector_end = -1;
        auto it            = _sectors.upper_bound(std::make_pair(offset, 0));

        if (!_sectors.empty() && it != _sectors.begin()) {
            --it;
            if (offset <= it->second) {
                sector_end = it->second;
            }
        }

        return sector_end;
    }

    /*
     * get the size of the data stored in this node
     * If the node is the home node then this is equals to
     * the real size of the file
     */
    [[nodiscard]] inline off64_t get_stored_size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return this->_get_stored_size();
    }

    /*
     * Insert the new sector automatically modifying the
     * existent _sectors if needed.
     *
     * Params:
     * off64_t new_start: the beginning of the sector to insert
     * off64_t new_end: the beginning of the sector to insert
     *
     * new_srart must be > new_end otherwise the behaviour
     * in undefined
     *
     */
    void insert_sector(off64_t new_start, off64_t new_end) {
        auto p = std::make_pair(new_start, new_end);
        std::lock_guard<std::mutex> lock(_mutex);

        if (_sectors.empty()) {
            _sectors.insert(p);
            return;
        }
        auto it_lbound = _sectors.upper_bound(p);
        if (it_lbound == _sectors.begin()) {
            if (new_end < it_lbound->first) {
                _sectors.insert(p);
            } else {
                auto it         = it_lbound;
                bool end_before = false;
                bool end_inside = false;
                while (it != _sectors.end() && !end_before && !end_inside) {
                    end_before = p.second < it->first;
                    if (!end_before) {
                        end_inside = p.second <= it->second;
                        if (!end_inside) {
                            ++it;
                        }
                    }
                }

                if (end_inside) {
                    p.second = it->second;
                    ++it;
                }
                _sectors.erase(it_lbound, it);
                _sectors.insert(p);
            }
        } else {
            --it_lbound;
            auto it = it_lbound;
            if (p.first <= it_lbound->second) {
                // new sector starts inside a sector
                p.first = it_lbound->first;
            } else { // in this way the sector will not be deleted
                ++it_lbound;
            }
            bool end_before = false;
            bool end_inside = false;
            while (it != _sectors.end() && !end_before && !end_inside) {
                end_before = p.second < it->first;
                if (!end_before) {
                    end_inside = p.second <= it->second;
                    if (!end_inside) {
                        ++it;
                    }
                }
            }

            if (end_inside) {
                p.second = it->second;
                ++it;
            }
            _sectors.erase(it_lbound, it);
            _sectors.insert(p);
        }
    }

    [[nodiscard]] inline bool is_closed() const {
        return _n_close_expected == -1 || _n_close == _n_close_expected;
    }

    [[nodiscard]] inline bool is_deletable() const { return _n_opens == 0 && _n_links <= 0; }

    [[nodiscard]] inline bool is_dir() const { return _directory; }

    inline void open() { _n_opens++; }

    void print(std::ostream &out_stream) const {
        out_stream << "_sectors" << std::endl;
        for (auto &sector : _sectors) {
            out_stream << "<" << sector.first << ", " << sector.second << ">" << std::endl;
        }
    }

    /*
     * From the manual:
     *
     * Adjust the file offset to the next location in the file
     * greater than or equal to offset containing data.  If
     * offset points to data, then the file offset is set to
     * offset.
     *
     * Fails if offset points past the end of the file.
     *
     */
    off64_t seek_data(off64_t offset) {
        if (_sectors.empty()) {
            if (offset == 0) {
                return 0;
            } else {
                return -1;
            }
        }
        auto it = _sectors.upper_bound(std::make_pair(offset, 0));
        if (it == _sectors.begin()) {
            return it->first;
        }
        --it;
        if (offset <= it->second) {
            return offset;
        } else {
            ++it;
            if (it == _sectors.end()) {
                return -1;
            } else {
                return it->first;
            }
        }
    }

    /*
     * From the manual:
     *
     * Adjust the file offset to the next hole in the file
     * greater than or equal to offset.  If offset points into
     * the middle of a hole, then the file offset is set to
     * offset.  If there is no hole past offset, then the file
     * offset is adjusted to the end of the file (i.e., there is
     * an implicit hole at the end of any file).
     *
     *
     * Fails if offset points past the end of the file.
     *
     */
    [[nodiscard]] off64_t seek_hole(off64_t offset) const {
        if (_sectors.empty()) {
            if (offset == 0) {
                return 0;
            } else {
                return -1;
            }
        }
        auto it = _sectors.upper_bound(std::make_pair(offset, 0));
        if (it == _sectors.begin()) {
            return offset;
        }
        --it;
        if (offset <= it->second) {
            return it->second;
        } else {
            ++it;
            if (it == _sectors.end()) {
                return -1;
            } else {
                return offset;
            }
        }
    }

    inline void remove_fd(int tid, int fd) {
        auto it = std::find(_threads_fd.begin(), _threads_fd.end(), std::make_pair(tid, fd));
        if (it != _threads_fd.end()) {
            _threads_fd.erase(it);
        }
    }

    /**
     * Save data inside the capio_file buffer
     * @param buffer
     * @return
     */
    inline void read_from_node(const std::string &dest, off64_t offset, off64_t buffer_size) {
        std::unique_lock<std::mutex> lock(_mutex);
        backend->recv_file(_buf + offset, dest, buffer_size);
        _data_avail_cv.notify_all();
    }

    inline void read_from_queue(SPSCQueue<char> &queue, size_t offset) {
        START_LOG(gettid(), "call()");

        std::unique_lock<std::mutex> lock(_mutex);
        queue.read(_buf + offset);
        _data_avail_cv.notify_all();
    }

    inline void read_from_queue(SPSCQueue<char> &queue, size_t offset, long int num_bytes) {
        START_LOG(gettid(), "call()");

        std::unique_lock<std::mutex> lock(_mutex);
        queue.read(_buf + offset, num_bytes);
        _data_avail_cv.notify_all();
    }

    inline void unlink() { _n_links--; }
};

#endif // CAPIO_SERVER_UTILS_CAPIO_FILE_HPP
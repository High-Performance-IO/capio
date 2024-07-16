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
#include "capio/queue.hpp"

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

  protected:
    std::string_view _committed      = CAPIO_FILE_COMMITTED_ON_TERMINATION;
    std::string_view _mode           = CAPIO_FILE_MODE_UPDATE;
    bool _home_node                  = false;
    int _n_links                     = 1;
    long int _n_close                = 0;
    long int _n_close_expected       = -1;
    int _n_opens                     = 0;
    off64_t _buf_size                = 0;
    bool _permanent                  = false;
    bool _directory                  = false;
    bool _complete                   = false; // whether the file is completed / committed
    std::filesystem::path _file_name = "";
    // vector of (tid, fd)
    std::vector<std::pair<int, int>> _threads_fd;
    char *_buf = nullptr; // buffer containing the data

    /*sync variables*/
    mutable std::mutex _mutex;
    mutable std::condition_variable _complete_cv;
    mutable std::condition_variable _data_avail_cv;

  public:
    enum seek_type { data, hole };
    virtual ~CapioFile() = default;
    explicit CapioFile(const std::filesystem::path &name)
        : _committed(CAPIO_FILE_COMMITTED_ON_TERMINATION), _permanent(false), _directory(false),
          _file_name(name) {
        if (backend != nullptr) {
            backend->notify_backend(Backend::createFile, _file_name, nullptr, 0, 0, false);
        }
    };

    CapioFile(std::filesystem::path name, const std::string_view &committed,
              const std::string_view &mode, bool directory, bool permanent,
              long int n_close_expected)
        : _committed(committed), _mode(mode), _n_close_expected(n_close_expected),
          _permanent(permanent), _directory(directory), _file_name(std::move(name)) {
        if (backend != nullptr) {
            backend->notify_backend(Backend::createFile, _file_name, nullptr, 0, 0, directory);
        }
    };

    CapioFile(std::filesystem::path name, const std::string_view &committed, bool directory,
              bool permanent, long int n_close_expected = -1)
        : _committed(committed), _n_close_expected(n_close_expected), _permanent(permanent),
          _directory(directory), _file_name(std::move(name)) {
        if (backend != nullptr) {
            backend->notify_backend(Backend::createFile, _file_name, nullptr, 0, 0, directory);
        }
    }

    bool first_write          = true;
    long int n_files          = 0;  // useful for directories
    long int n_files_expected = -1; // useful for directories

    CapioFile(const CapioFile &)                                   = delete;
    CapioFile &operator=(const CapioFile &)                        = delete;
    virtual ssize_t get_file_size()                                = 0;
    virtual inline bool is_complete()                              = 0;
    virtual inline void wait_for_completion()                      = 0;
    virtual void commit()                                          = 0;
    virtual void inline close()                                    = 0;
    virtual void allocate(bool home_node)                          = 0;
    virtual char *realloc(off64_t data_size, void *previus_buffer) = 0;
    virtual inline char *get_buffer(off64_t offset,
                                    ssize_t size)   = 0; // TOGLIERE. FARE DIVENTARE READ
    virtual inline void set_file_size(off64_t size) = 0;
    virtual inline off64_t get_buf_size()           = 0;
    virtual inline off64_t get_stored_size()        = 0;

    virtual off64_t get_sector_end(off64_t offset)                                     = 0; // BOH
    virtual inline const std::set<std::pair<off64_t, off64_t>, compare> &get_sectors() = 0; // BOH

    virtual void insert_sector(off64_t new_start, off64_t new_end)                           = 0;
    virtual off64_t seek(CapioFile::seek_type type, off64_t offset)                          = 0;
    virtual inline void read_from_node(const std::string &dest, off64_t offset, off64_t buffer_size,
                                       const std::filesystem::path &file_path)               = 0;
    virtual inline void read_from_queue(SPSCQueue &queue, size_t offset, long int num_bytes) = 0;

    inline void add_fd(int tid, int fd) { _threads_fd.emplace_back(tid, fd); }

    [[nodiscard]] inline const std::string_view &get_committed() const { return _committed; }
    [[nodiscard]] inline const std::string_view &get_mode() const { return _mode; }
    [[nodiscard]] inline bool is_closed() const {
        return _n_close_expected == -1 || _n_close == _n_close_expected;
    }
    inline void open() { _n_opens++; }
    [[nodiscard]] inline bool is_dir() const { return _directory; }
    [[nodiscard]] inline bool is_deletable() const { return _n_opens == 0 && _n_links <= 0; }
    inline void unlink() { _n_links--; }
    inline void remove_fd(int tid, int fd) {
        auto it = std::find(_threads_fd.begin(), _threads_fd.end(), std::make_pair(tid, fd));
        if (it != _threads_fd.end()) {
            _threads_fd.erase(it);
        }
    }

    [[nodiscard]] inline const std::vector<std::pair<int, int>> &get_fds() { return _threads_fd; }

    inline void wait_for_data(long offset) {
        START_LOG(gettid(), "call()");
        LOG("Thread waiting for data to be available");
        std::unique_lock<std::mutex> lock(_mutex);
        _data_avail_cv.wait(lock, [offset, this] {
            return (offset <= this->get_stored_size()) || this->_complete;
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
};

#include "capio_fs_file.hpp"
#include "capio_mem_file.hpp"

#endif // CAPIO_SERVER_UTILS_CAPIO_FILE_HPP
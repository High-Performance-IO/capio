#ifndef CAPIO_MEM_FILE_HPP
#define CAPIO_MEM_FILE_HPP

#include "capio_file.hpp"

class CapioMemFile : public CapioFile {
  private:
    // _fd is useful only when the file is memory-mapped
    int _fd = -1;

    // _sectors stored in memory of the files (only the home node is forced to
    // be up to date)
    std::set<std::pair<off64_t, off64_t>, compare> _sectors;

    inline off64_t _get_stored_size() const {
        auto it = _sectors.rbegin();
        return (it == _sectors.rend()) ? 0 : it->second;
    }

    std::size_t real_file_size = 0;

  public:
    CapioMemFile(const CapioFile &)               = delete;
    CapioMemFile &operator=(const CapioMemFile &) = delete;

    /*
     * file size in the home node. In a given moment could not be up-to-date.
     * This member is useful because a node different from the home node
     * could need to known the size of the file but not its content
     */

    inline void set_file_size(off64_t size) override { real_file_size = size; };

    explicit CapioMemFile(const std::filesystem::path &name) : CapioFile(name) {}

    CapioMemFile(std::filesystem::path name, const std::string_view &committed,
                 const std::string_view &mode, bool directory, long int n_file_expected,
                 bool permanent, off64_t init_size, long int n_close_expected)
        : CapioFile(std::move(name), committed, mode, directory, permanent, n_close_expected) {
        n_files_expected = n_file_expected + 2;
        _buf_size        = init_size;
    }

    CapioMemFile(std::filesystem::path name, bool directory, bool permanent, off64_t init_size,
                 long int n_close_expected = -1)
        : CapioFile(name, CAPIO_FILE_COMMITTED_ON_TERMINATION, directory, permanent,
                    n_close_expected) {
        _buf_size = init_size;
    }

    ~CapioMemFile() {
        START_LOG(gettid(), "call()");

        LOG("Deleting capio_file");
        if (_permanent && _home_node) {
            if (_directory) {
                free(_buf);
            } else {
                int res = munmap(_buf, _buf_size);
                if (res == -1) {
                    ERR_EXIT("munmap CapioFile");
                }
            }
        } else {
            free(_buf);
        }
    }

    [[nodiscard]] inline bool is_complete() override {
        START_LOG(gettid(), "capio_file is complete? %s", this->_complete ? "true" : "false");

        std::lock_guard<std::mutex> lg(_mutex);
        return this->_complete;
    }

    inline void wait_for_completion() override {
        START_LOG(gettid(), "call()");

        LOG("Thread waiting for file to be committed");
        std::unique_lock<std::mutex> lock(_mutex);
        _complete_cv.wait(lock, [this] { return _complete; });
    }

    inline void close() override {
        _n_close++;
        _n_opens--;
    }

    void commit() override {
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
    void allocate(bool home_node) override {
        START_LOG(gettid(), "call(path=%s, home_node=%s)", _file_name.c_str(),
                  home_node ? "true" : "false");

        std::lock_guard<std::mutex> lock(_mutex);

        // allocate buffer only if it is required
        if (_buf != nullptr) {
            return;
        }

        _home_node = home_node;
        if (_permanent && home_node) {
            if (_directory) {
                std::filesystem::create_directory(_file_name);
                std::filesystem::permissions(_file_name, std::filesystem::perms::owner_all);
                _buf = static_cast<char *>(malloc(_buf_size * sizeof(char)));
            } else {
                LOG("creating mem mapped file");
                _fd = ::open(_file_name.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
                if (_fd == -1) {
                    ERR_EXIT("open %s CapioFile constructor", _file_name.c_str());
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
            _buf = static_cast<char *>(malloc(_buf_size * sizeof(char)));
        }
    }

    char *realloc(off64_t data_size, void *previus_buffer) override {
        START_LOG(gettid(), "call()");

        LOG("File is stored in memory. reallocating buffer. _buf == nullptr ? %s",
            _buf == nullptr ? "Yes" : "No");
        off64_t double_size = _buf_size * 2;
        off64_t new_size    = data_size > double_size ? data_size : double_size;

        std::lock_guard<std::mutex> lock(_mutex);
        _buf      = static_cast<char *>(std::realloc(_buf, new_size));
        _buf_size = new_size;
        return _buf;
    }

    inline char *get_buffer(off64_t offset = 0,
                            ssize_t size   = CAPIO_DEFAULT_FILE_INITIAL_SIZE) override {
        return _buf;
    }

    [[nodiscard]] inline off64_t get_buf_size() override { return _buf_size; }

    ssize_t get_file_size() override {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_sectors.empty()) {
            return _sectors.rbegin()->second;
        } else {
            return 0;
        }
    }

    /*
     * Returns the offset to the end of the sector
     * if the offset parameter is inside the
     * sector, -1 otherwise
     *
     */
    [[nodiscard]] off64_t get_sector_end(off64_t offset) override {
        START_LOG(gettid(), "call(offset=%ld)", offset);

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

    [[nodiscard]] inline const std::set<std::pair<off64_t, off64_t>, compare> &
    get_sectors() override {
        return _sectors;
    }

    /*
     * get the size of the data stored in this node
     * If the node is the home node then this is equals to
     * the real size of the file
     */
    [[nodiscard]] inline off64_t get_stored_size() override {
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
     * new_start must be > new_end otherwise the behaviour
     * in undefined
     *
     */
    void insert_sector(off64_t new_start, off64_t new_end) override {
        START_LOG(gettid(), "call(new_start=%ld, new_end=%ld)", new_start, new_end);

        auto p = std::make_pair(new_start, new_end);
        std::lock_guard<std::mutex> lock(_mutex);

        if (_sectors.empty()) {
            LOG("Insert sector <%ld, %ld>", p.first, p.second);
            _sectors.insert(p);
            return;
        }
        auto it_lbound = _sectors.upper_bound(p);
        if (it_lbound == _sectors.begin()) {
            if (new_end < it_lbound->first) {
                LOG("Insert sector <%ld, %ld>", p.first, p.second);
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
                LOG("Insert sector <%ld, %ld>", p.first, p.second);
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
            LOG("Insert sector <%ld, %ld>", p.first, p.second);
            _sectors.insert(p);
        }
    }

    /*
     * From the manual:
     * Adjust the file offset to the next location in the file greater than or equal to offset
     * containing data. If offset points to data, then the file offset is set to offset. Fails
     * if offset points past the end of the file.
     * From the manual:
     * Adjust the file offset to the next hole in the file greater than or equal to offset.
     * If offset points into  the middle of a hole, then the file offset is set to offset.
     * If there is no hole past offset, then the file offset is adjusted to the end of the
     * file (i.e., there is an implicit hole at the end of any file).
     * Fails if offset points past the end of the file.
     */
    off64_t seek(CapioFile::seek_type type, off64_t offset) override {
        START_LOG(gettid(), "call(type=%s, offset=%ld)",
                  type == CapioFile::seek_type::data ? "data" : "hole", offset);

        if (_sectors.empty()) {
            return offset == 0 ? 0 : -1;
        }
        auto it = _sectors.upper_bound(std::make_pair(offset, 0));
        if (it == _sectors.begin()) {
            return type == seek_type::data ? it->first : offset;
        }
        --it;
        if (offset <= it->second) {
            return type == seek_type::data ? offset : it->first;
        } else {
            ++it;
            if (it == _sectors.end()) {
                return -1;
            } else {
                return type == seek_type::data ? it->first : offset;
            }
        }
    }

    /**
     * Save data inside the capio_file buffer
     * @param buffer
     * @return
     */
    inline void read_from_node(const std::string &dest, off64_t offset, off64_t buffer_size,
                               const std::filesystem::path &file_path) override {
        START_LOG(gettid(), "call()");
        std::unique_lock<std::mutex> lock(_mutex);
        backend->recv_file(_buf + offset, dest, buffer_size, offset, file_path);

        _data_avail_cv.notify_all();
    }

    inline void read_from_queue(SPSCQueue &queue, size_t offset, long int num_bytes) override {
        START_LOG(gettid(), "call()");
        std::unique_lock<std::mutex> lock(_mutex);
        queue.read(_buf + offset, num_bytes);
        backend->notify_backend(Backend::backendActions::writeFile, _file_name, _buf, offset,
                                num_bytes, this->_directory);
        _data_avail_cv.notify_all();
    }
};
#endif // CAPIO_MEM_FILE_HPP

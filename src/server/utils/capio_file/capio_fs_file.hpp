#ifndef CAPIO_FS_FILE_HPP
#define CAPIO_FS_FILE_HPP

#include "capio_file.hpp"

class CapioFsFile : public CapioFile {
  private:
    std::filesystem::path _metadata_file_name;
    std::size_t actual_file_buffer_size_fs = CAPIO_DEFAULT_FILE_INITIAL_SIZE;

    /**
     * Following fuctions used to store and retrive metadata of a given capio file
     */

    void write_metadata_to_fs() const {
        auto f = std::fopen(_metadata_file_name.c_str(), "w");
        std::fprintf(f, "%d %ld %d %d", _n_links, _n_close, _n_opens, _complete ? 1 : 0);
        std::fclose(f);
    }

    std::tuple<int, long, int, bool> read_metadata_from_fs() const {
        long size, close;
        int links, opens, complete;
        auto f = fopen(_metadata_file_name.c_str(), "r");
        std::fscanf(f, "%d %ld %d %d", &links, &close, &opens, &complete);
        return {links, close, opens, complete};
    }

  public:
    CapioFsFile(const CapioFile &)              = delete;
    CapioFsFile &operator=(const CapioFsFile &) = delete;

    explicit CapioFsFile(std::filesystem::path &name) : CapioFile(name) {
        _metadata_file_name = _file_name.append(".capio");
    }

    CapioFsFile(std::filesystem::path name, const std::string_view &committed,
                const std::string_view &mode, bool directory, long int n_file_expected,
                bool permanent, off64_t init_size, long int n_close_expected)
        : CapioFile(std::move(name), committed, mode, directory, permanent, n_close_expected) {
        n_files_expected    = n_file_expected + 2;
        _metadata_file_name = _file_name.append(".capio");
    }

    CapioFsFile(std::filesystem::path name, bool directory, bool permanent, off64_t init_size,
                long int n_close_expected = -1)
        : CapioFile(name, CAPIO_FILE_COMMITTED_ON_TERMINATION, directory, permanent,
                    n_close_expected) {
        _metadata_file_name = _file_name.append(".capio");
    }

    ~CapioFsFile() override {
        START_LOG(gettid(), "call()");
        LOG("Memory cleanup not required as no buffer has been allocated");
    }

    inline void set_file_size(off64_t size) override{};

    [[nodiscard]] inline bool is_complete() override {
        START_LOG(gettid(), "capio_file is complete? %s", this->_complete ? "true" : "false");
        return std::get<3>(read_metadata_from_fs());
    }

    inline void wait_for_completion() override {
        START_LOG(gettid(), "call()");

        timespec t{};
        t.tv_nsec = 1000;
        while (!is_complete()) {
            nanosleep(&t, nullptr);
        }
    }

    inline void close() override {
        _n_close++;
        _n_opens--;
        write_metadata_to_fs();
    }

    void commit() override { START_LOG(gettid(), "call()"); }

    void allocate(bool home_node) override {
        START_LOG(gettid(), "call(path=%s, home_node=%s)", _file_name.c_str(),
                  home_node ? "true" : "false");

        LOG("Creating file buffer on FS plus a small buffer for data movement inside capio "
            "server");
        if (_buf == nullptr) {
            _buf = new char[CAPIO_DEFAULT_FILE_INITIAL_SIZE];
        }
        backend->notify_backend(Backend::backendActions::createFile, _file_name, nullptr, 0, 0,
                                this->_directory);
    }

    char *realloc(off64_t data_size, void *previus_buffer) override {
        START_LOG(gettid(), "call()");

        return static_cast<char *>(previus_buffer);
    }

    inline char *get_buffer(off64_t offset, ssize_t size) override {

        if (size > actual_file_buffer_size_fs) {
            actual_file_buffer_size_fs = size;
            delete[] _buf;
            _buf = new char[size];
        }
        backend->notify_backend(Backend::backendActions::readFile, _file_name, _buf, offset, size,
                                this->_directory);
        return _buf;
    }

    ssize_t get_file_size() override {
        try {
            if (std::filesystem::exists(this->_file_name)) {
                return backend->notify_backend(Backend::fileSize, this->_file_name, nullptr, 0, 0,
                                               this->is_dir());
            }
        } catch (std::exception &e) {
            return 0;
        }
        return 0;
    }

    inline off64_t get_buf_size() override { return get_stored_size(); };

    off64_t get_sector_end(off64_t offset) override {
        START_LOG(gettid(), "call(offset=%ld)", offset);
        return 0;
    }

    inline const std::set<std::pair<off64_t, off64_t>, compare> &get_sectors() override {
        return {};
    }

    inline off64_t get_stored_size() override {
        return backend->notify_backend(Backend::fileSize, _file_name, nullptr, 0, 0, _directory);
    }

    void insert_sector(off64_t new_start, off64_t new_end) override {}

    off64_t seek(CapioFile::seek_type type, off64_t offset) override {
        return backend->notify_backend(Backend::seekFile, _file_name, nullptr, offset, 0,
                                       _directory);
    }

    inline void read_from_node(const std::string &dest, off64_t offset, off64_t buffer_size,
                               const std::filesystem::path &file_path) override {}

    virtual inline void read_from_queue(SPSCQueue &queue, size_t offset,
                                        long int num_bytes) override {}
};

#endif // CAPIO_FS_FILE_HPP

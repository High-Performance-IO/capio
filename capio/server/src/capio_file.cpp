#include "server/include/storage/capio_file.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "../include/storage/memory_allocator.hpp"
#include "remote/backend.hpp"
#include "server/include/utils/common.hpp"
#include "utils/shared_mutex.hpp"

extern Backend *backend;

bool CapioFile::compareSectors::operator()(const std::pair<off64_t, off64_t> &lhs,
                                           const std::pair<off64_t, off64_t> &rhs) const {
    return (lhs.first < rhs.first);
}

CapioFile::CapioFile() = default;

CapioFile::CapioFile(const bool directory, const unsigned int n_files_expected,
                     const bool permanent, const off64_t init_size, const int n_close_expected)
    : _requested_size(init_size), _n_files_expected(n_files_expected + 2),
      _n_close_expected(n_close_expected), _directory(directory), _permanent(permanent) {}

CapioFile::CapioFile(const bool directory, const bool permanent, const off64_t init_size,
                     const unsigned int n_close_expected)
    : _requested_size(init_size), _n_close_expected(n_close_expected), _directory(directory),
      _permanent(permanent) {}

CapioFile::~CapioFile() {
    START_LOG(gettid(), "call()");
    LOG("Deleting capio_file");
}

bool CapioFile::isCommitted() const {
    START_LOG(gettid(), "capio_file is complete? %s", this->_committed ? "true" : "false");
    std::lock_guard lg(_mutex);
    return this->_committed;
}

void CapioFile::waitForData(long offset) const {
    START_LOG(gettid(), "call()");
    LOG("Thread waiting for data to be available");
    std::unique_lock lock(_mutex);
    _data_avail_cv.wait(lock,
                        [offset, this] { return this->_getStoredSize() >= offset || _committed; });
}

void CapioFile::setCommitted(const bool commit) {
    START_LOG(gettid(), "setting capio_file._complete=%s", commit ? "true" : "false");
    std::lock_guard lg(_mutex);
    if (this->_committed != commit) {
        this->_committed = commit;
        if (this->_committed) {
            _committed_cv.notify_all();
            _data_avail_cv.notify_all();
        }
    }
}

void CapioFile::addFd(int tid, int fd) {
    START_LOG(gettid(), "call(tid=%d, fd=%d)", tid, fd);
    std::lock_guard lg(_mutex_threads_fd);
    _threads_fd.emplace_back(tid, fd);
}

void CapioFile::waitForCommit() const {
    START_LOG(gettid(), "call()");
    LOG("Thread waiting for file to be committed");
    std::unique_lock lock(_mutex);
    _committed_cv.wait(lock, [this] { return _committed; });
}

void CapioFile::close() {
    START_LOG(gettid(), "call()");
    ++_n_close;
    --_n_opens;
    LOG("AFTER: _n_close=%d / _n_opens=%d", _n_close.load(), _n_opens.load());
}

void CapioFile::dump() {
    START_LOG(gettid(), "call()");

    if (_permanent && !_directory && _home_node) {
        const off64_t size = getFileSize();
        if (!_buffer.commit(size)) {
            ERR_EXIT("ftruncate/close commit capio_file");
        }
    }
}

void CapioFile::createBuffer(const std::filesystem::path &path, const bool home_node) {
    START_LOG(gettid(), "call(path=%s, home_node=%s)", path.c_str(), home_node ? "true" : "false");
    std::lock_guard lock(_mutex);
    if (this->_buffer.empty()) {
        _home_node = home_node;
        if (_permanent && home_node) {
            if (_directory) {
                std::filesystem::create_directory(path);
                std::filesystem::permissions(path, std::filesystem::perms::owner_all);
                _buffer.allocateHeap(_requested_size);
            } else {
                LOG("creating mem mapped file");
                if (!_buffer.allocateMapped(path, _requested_size)) {
                    ERR_EXIT("mmap %s CapioFile constructor", path.c_str());
                }
            }
        } else {
            _buffer.allocateHeap(_requested_size);
        }
    }
}

void CapioFile::_memcopyCapioFile(char *new_p, const char *old_p) const {
    // NOTE: do not lock here: this method is private and when reaching control in this function,
    //       the caller has already locked the mutex
    START_LOG(gettid(), "call()");
    for (const auto &[lbound, ubound] : _sectors) {
        memcpy(new_p + lbound, old_p + lbound, ubound - lbound);
    }
}

char *CapioFile::expandBuffer(const off64_t data_size) {
    std::lock_guard lock(_mutex);
    return _buffer.resize(
        data_size, [this](char *new_p, const char *old_p) { _memcopyCapioFile(new_p, old_p); });
}

// TODO: This is a memory leak that may lead to race conditions. remove exposure of internal buffer
char *CapioFile::getBuffer() const { return _buffer.data(); }

off64_t CapioFile::getBufSize() const {
    // until allocation the requested init size is authoritative (old code reported _buf_size=init)
    return _buffer.empty() ? _requested_size : _buffer.size();
}

// TODO: This is a memory leak that may lead to race conditions. remove exposure of internal buffer
const std::vector<std::pair<int, int>> &CapioFile::getFds() const { return _threads_fd; }

off64_t CapioFile::getFileSize() const {
    const shared_lock_guard slg(_mutex_sectors);
    if (!_sectors.empty()) {
        return _sectors.rbegin()->second;
    } else {
        return 0;
    }
}

off64_t CapioFile::getSectorEnd(off64_t offset) const {
    START_LOG(gettid(), "call(offset=%ld)", offset);

    const shared_lock_guard slg(_mutex_sectors);

    if (_sectors.empty()) {
        return -1;
    }

    if (auto it = _sectors.upper_bound(std::make_pair(offset, 0)); it != _sectors.begin()) {
        --it;
        if (offset <= it->second) {
            return it->second;
        }
    }

    return -1;
}
// TODO: This is a memory leak that may lead to race conditions. remove exposure of internal buffer
const std::set<std::pair<off64_t, off64_t>, CapioFile::compareSectors> &
CapioFile::getSectors() const {
    return _sectors;
}

off64_t CapioFile::getStoredSize() const {
    std::lock_guard lock(_mutex);
    return this->_getStoredSize();
}

void CapioFile::insertSector(off64_t new_start, off64_t new_end) {
    START_LOG(gettid(), "call(new_start=%ld, new_end=%ld)", new_start, new_end);
    std::lock_guard lock(_mutex_sectors);

    // First sector whose start >= new_start
    auto it_start = _sectors.lower_bound({new_start, LLONG_MIN});

    // Check if the previous sector reaches into our range
    if (it_start != _sectors.begin()) {
        if (const auto it_prev = std::prev(it_start); it_prev->second >= new_start) {
            new_start = it_prev->first;
            new_end   = std::max(new_end, it_prev->second);
            it_start  = it_prev;
        }
    }

    // First sector whose start > new_end (adjacent sectors included via +1)
    const auto it_end = _sectors.upper_bound({new_end + 1, LLONG_MIN});

    // Absorb the right edge of the last overlapping sector if needed
    if (it_end != _sectors.begin()) {
        if (const auto it_last = std::prev(it_end); it_last->second > new_end) {
            new_end = it_last->second;
        }
    }

    _sectors.erase(it_start, it_end);
    LOG("Insert sector <%ld, %ld>", new_start, new_end);
    _sectors.emplace(new_start, new_end);
}

bool CapioFile::closed() const {
    return _n_close_expected == 0 || _n_close == static_cast<int>(_n_close_expected);
}

bool CapioFile::deletable() const { return _n_opens <= 0; }

bool CapioFile::isDirectory() const { return _directory; }

void CapioFile::open() { ++_n_opens; }

off64_t CapioFile::seekData(off64_t offset) const {
    START_LOG(gettid(), "call(offset=%ld)", offset);
    const shared_lock_guard slg(_mutex_sectors);
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

off64_t CapioFile::seekHole(off64_t offset) const {
    START_LOG(gettid(), "call(offset=%ld)", offset);
    const shared_lock_guard slg(_mutex_sectors);
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

void CapioFile::removeFd(int tid, int fd) {
    std::lock_guard lock(_mutex_threads_fd);
    if (const auto it = std::find(_threads_fd.begin(), _threads_fd.end(), std::make_pair(tid, fd));
        it != _threads_fd.end()) {
        _threads_fd.erase(it);
    }
}

void CapioFile::readFromNode(const std::string &dest, off64_t offset, off64_t buffer_size) const {
    std::lock_guard lock(_mutex);
    backend->recv_file(_buffer.data() + offset, buffer_size, dest);
    _data_avail_cv.notify_all();
}

void CapioFile::readFromQueue(SPSCQueue &queue, size_t offset, long int num_bytes) const {
    START_LOG(gettid(), "call()");

    std::lock_guard lock(_mutex);
    queue.read(_buffer.data() + offset, num_bytes);
    _data_avail_cv.notify_all();
}

off64_t CapioFile::_getStoredSize() const {
    const shared_lock_guard slg(_mutex_sectors);
    const auto it = _sectors.rbegin();
    return (it == _sectors.rend()) ? 0 : it->second;
}

bool CapioFile::bufferToAllocate() const {
    std::lock_guard lg(_mutex);
    return _buffer.empty();
}

off64_t CapioFile::getRealFileSize() const { return this->_real_file_size; }

void CapioFile::setRealFileSize(const off64_t size) { this->_real_file_size = size; }

bool CapioFile::isFirstWrite() const { return this->_first_write; }

void CapioFile::registerFirstWrite() { this->_first_write = false; }

void CapioFile::incrementDirectoryFileCount(const int count) {
    this->_n_files += count;
    this->_data_avail_cv.notify_all();
}

unsigned int CapioFile::getCurrentDirectoryFileCount() const { return this->_n_files; }

unsigned int CapioFile::getDirectoryExpectedFileCount() const { return this->_n_files_expected; }

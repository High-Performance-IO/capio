#ifndef CAPIO_SERVER_UTILS_CACHE
#define CAPIO_SERVER_UTILS_CACHE

#include <unordered_map>
#include <vector>

#include "common/dirent.hpp"
#include "common/queue.hpp"

#include "requests.hpp"

/// Whether CAPIO descriptors @p fd and @p other are of one file
inline bool same_file(const int fd, const int other) {
    return fd == other ||
           (exists_capio_fd(other) && get_capio_fd_path(fd) == get_capio_fd_path(other));
}

class ReadCache {
  private:
    /// What was prefetched of one file and not read yet: data[offset, size). Copied out of the
    /// queue, whose slots the server reuses.
    struct Line {
        std::vector<char> data;
        off64_t size = 0, offset = 0;
        unsigned long last_use = 0;
    };
    /// Files cached at once: a tool that reads several files in turn (Montage's mBackground an
    /// image and its area, mAdd four of each) keeps a line for each, instead of losing it at
    /// every switch
    static constexpr std::size_t MAX_LINES = 8;

    long _tid;
    off64_t _max_line_size;
    SPSCQueue _queue;
    std::unordered_map<int, Line> _lines;
    unsigned long _clock = 0;

    // The server's offset of fd runs ahead of the client's by what is still cached: put it back.
    inline void _flush(const int fd, Line &line) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d)", fd);
        if (line.offset != line.size) {
            line.size = line.offset = 0;
            seek_request(fd, get_capio_fd_offset(fd), _tid);
        }
    }

    inline Line &_line(const int fd) {
        auto it = _lines.find(fd);
        if (it == _lines.end()) {
            if (_lines.size() >= MAX_LINES) {
                auto lru = _lines.begin();
                for (auto i = _lines.begin(); i != _lines.end(); ++i) {
                    if (i->second.last_use < lru->second.last_use) {
                        lru = i;
                    }
                }
                LOG("evicting the line of fd %d", lru->first);
                _flush(lru->first, lru->second);
                _lines.erase(lru);
            }
            it = _lines.emplace(fd, Line{}).first;
            it->second.data.resize(_max_line_size);
        }
        it->second.last_use = ++_clock;
        return it->second;
    }

    static inline void _read(Line &line, void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            memcpy(buffer, line.data.data() + line.offset, count);
            LOG("Read %ld. adding it to the line offset of value %ld", count, line.offset);
            line.offset += count;
        }
    }

  public:
    ReadCache(long tid, off64_t lines, off64_t line_size, const std::string &workflow_name)
        : _tid(tid), _max_line_size(line_size),
          _queue(SHM_SPSC_PREFIX_READ + std::to_string(tid), lines, line_size, workflow_name) {}

    /// Drop every cached line, telling the server where each file's offset really is
    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");
        for (auto &[fd, line] : _lines) {
            _flush(fd, line);
        }
        _lines.clear();
    }

    /// Drop the cached line of @p fd only: a seek or a close of one file leaves the others'
    inline void flush(const int fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d)", fd);
        if (const auto it = _lines.find(fd); it != _lines.end()) {
            _flush(fd, it->second);
            _lines.erase(it);
        }
    }

    // Check whether this instance caches data from the file path associated with the given
    // fd or not
    [[nodiscard]] bool cachesFileOf(const int fd) const {
        for (const auto &[cached, line] : _lines) {
            if (line.offset != line.size && same_file(fd, cached)) {
                return true;
            }
        }
        return false;
    }

    /// Drop the lines of every descriptor of the file of @p fd, which a write makes stale
    inline void flushFileOf(const int fd) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d)", fd);
        for (auto it = _lines.begin(); it != _lines.end();) {
            if (same_file(fd, it->first)) {
                _flush(it->first, it->second);
                it = _lines.erase(it);
            } else {
                ++it;
            }
        }
    }

    inline off64_t read(int fd, void *buffer, off64_t count, bool is_getdents, bool is64bit) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, count=%ld, is_getdents=%s, is64bit=%s)",
                  fd, count, is_getdents ? "true" : "false", is64bit ? "true" : "false");

        Line &line              = _line(fd);
        off64_t remaining_bytes = line.size - line.offset;
        off64_t file_offset     = get_capio_fd_offset(fd);
        off64_t bytes_read;

        if (is_getdents) {
            auto dirent_size = static_cast<off64_t>(sizeof(linux_dirent64));
            count            = (count / dirent_size) * dirent_size;
        }

        auto read_size = count - remaining_bytes;
        LOG("Read() will need to read %ld bytes", read_size);

        if (count <= remaining_bytes) {
            LOG("count %ld <= remaining_bytes %ld", count, remaining_bytes);
            _read(line, buffer, count);
            bytes_read = count;
        } else {
            LOG("count %ld > remaining_bytes %ld", count, remaining_bytes);
            _read(line, buffer, remaining_bytes);
            buffer = reinterpret_cast<char *>(buffer) + remaining_bytes;

            // NOTE: if getdents send a request for exactly the correct amount of data.
            if (read_size > _max_line_size || is_getdents) {
                LOG("count - remaining_bytes %ld > _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading exactly requested size");
                off64_t end_of_read = is_getdents ? getdents_request(fd, read_size, is64bit, _tid)
                                                  : read_request(fd, read_size, _tid);
                bytes_read          = end_of_read - file_offset;
                _queue.read(reinterpret_cast<char *>(buffer), bytes_read);
                line.size = line.offset = 0;
            } else {
                LOG("count - remaining_bytes %ld <= _max_line_size %ld", read_size, _max_line_size);
                LOG("Reading more to use pre fetching and caching");
                off64_t end_of_read = is_getdents
                                          ? getdents_request(fd, _max_line_size, is64bit, _tid)
                                          : read_request(fd, _max_line_size, _tid);
                LOG("request return value is %ld", end_of_read);
                line.size   = end_of_read - file_offset - remaining_bytes;
                line.offset = 0;
                LOG("ReaderCache actual size, after requested read is: %ld bytes", line.size);
                if (line.size > 0) {
                    LOG("Fetching data from shm _queue");
                    memcpy(line.data.data(), _queue.fetch(), line.size);
                }
                if (read_size < line.size) {
                    LOG("count - remaining_bytes %ld < line size %ld", read_size, line.size);
                    _read(line, buffer, read_size);
                    bytes_read = count;
                } else {
                    LOG("count - remaining_bytes %ld >= line size %ld", read_size, line.size);
                    _read(line, buffer, line.size);
                    bytes_read = remaining_bytes + line.size;
                }
            }
        }
        LOG("%ld bytes have been read. setting fd offset to %ld", bytes_read,
            file_offset + bytes_read);
        set_capio_fd_offset(fd, file_offset + bytes_read);
        return bytes_read;
    }
};

class WriteCache {
  private:
    char *_cache;
    long _tid;
    int _fd;
    off64_t _max_line_size, _actual_size;
    SPSCQueue _queue;

    inline void _write(off64_t count, const void *buffer) {
        START_LOG(capio_syscall(SYS_gettid), "call(count=%ld)", count);

        if (count > 0) {
            if (_cache == nullptr) {
                _cache = _queue.reserve();
            }
            memcpy(_cache + _actual_size, buffer, count);
            _actual_size += count;
            if (_actual_size == _max_line_size) {
                flush();
            }
        }
    }

  public:
    WriteCache(long tid, off64_t lines, off64_t line_size, const std::string &workflow_name)
        : _cache(nullptr), _tid(tid), _fd(-1), _max_line_size(line_size), _actual_size(0),
          _queue(SHM_SPSC_PREFIX_WRITE + std::to_string(tid), lines, line_size, workflow_name) {}

    inline void flush() {
        START_LOG(capio_syscall(SYS_gettid), "call()");

        if (_actual_size != 0) {
            write_request(_fd, _actual_size, _tid);
            _cache       = nullptr;
            _actual_size = 0;
        }
    }

    /// Whether this cache holds data not yet sent of the file of @p fd
    [[nodiscard]] bool cachesFileOf(const int fd) const {
        return _actual_size != 0 && same_file(fd, _fd);
    }

    inline void write(int fd, const void *buffer, off64_t count) {
        START_LOG(capio_syscall(SYS_gettid), "call(fd=%d, buffer=0x%08x, count=%ld)", fd, buffer,
                  count);

        if (_fd != fd) {
            LOG("changed fd from %d to %d: flushing", _fd, fd);
            flush();
            _fd = fd;
        }

        if (count <= _max_line_size - _actual_size) {
            LOG("count %ld <= _max_line_size - _actual_size %ld", count,
                _max_line_size - _actual_size);
            _write(count, buffer);
        } else {
            LOG("count %ld > _max_line_size - _actual_size %ld", count,
                _max_line_size - _actual_size);
            flush();
            if (count - _actual_size > _max_line_size) {
                LOG("count - _actual_size %ld > _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                write_request(_fd, count, _tid);
                _queue.write(reinterpret_cast<const char *>(buffer), count);
            } else {
                LOG("count - _actual_size %ld <= _max_line_size %ld", count - _actual_size,
                    _max_line_size);
                _write(count, buffer);
            }
        }

        set_capio_fd_offset(fd, get_capio_fd_offset(fd) + count);
    }
};

inline thread_local WriteCache *write_cache;
inline thread_local ReadCache *read_cache;

#endif // CAPIO_SERVER_UTILS_CACHE

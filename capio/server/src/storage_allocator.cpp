#include "server/include/storage/allocator.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "common/syscall.hpp"

namespace {

#if !defined(CAPIO_MEMORY_CHECKS_NONE) // the NONE config returns early and uses none of these
constexpr uint64_t kUnlimited            = UINT64_MAX;
// cgroup v1 reports "no limit" as PAGE_COUNTER_MAX (~2^63), v2 as "max"
constexpr uint64_t kV1UnlimitedThreshold = 1ULL << 62;

/// cgroup memory accounting for this process, resolved once on first use.
/// usage_fd/stat_fd stay open and are re-read with pread on every call.
struct CgroupMem {
    int usage_fd   = -1;           ///< memory.current (v2) / memory.usage_in_bytes (v1)
    int stat_fd    = -1;           ///< memory.stat
    uint64_t limit = kUnlimited;   ///< smallest limit on the path to the root
    std::string_view inactive_key; ///< reclaimable page cache key in memory.stat
    bool active() const { return usage_fd >= 0; }
};

/// pread the whole file into buf. Returns the number of bytes read (0 on error).
size_t pread_all(int fd, char *buf, size_t cap) {
    size_t len = 0;
    while (len < cap) {
        const ssize_t n = pread(fd, buf + len, cap - len, static_cast<off_t>(len));
        if (n <= 0) {
            break;
        }
        len += static_cast<size_t>(n);
    }
    return len;
}

/// Parse a number or "max" from the start of text.
bool parse_u64(std::string_view text, uint64_t &out) {
    if (text.substr(0, 3) == "max") {
        out = kUnlimited;
        return true;
    }
    return std::from_chars(text.data(), text.data() + text.size(), out).ec == std::errc{};
}

bool pread_u64(int fd, uint64_t &out) {
    char buf[32];
    const size_t len = pread_all(fd, buf, sizeof(buf));
    return len > 0 && parse_u64({buf, len}, out);
}

/// Read "<number>|max" from a one-shot file. Cold path: opened and closed here.
bool read_u64(const std::string &path, uint64_t &out) {
    std::ifstream f(path);
    std::string token;
    if (!(f >> token)) {
        return false;
    }
    return parse_u64(token, out);
}

/// Find "<key> <value>" at the start of a line (so "inactive_file" does not match
/// "total_inactive_file") or "<key>: <value>" as in /proc/meminfo when key ends with ':'.
bool find_key(std::string_view text, std::string_view key, uint64_t &out) {
    for (size_t pos = 0; (pos = text.find(key, pos)) != std::string_view::npos; pos += key.size()) {
        const size_t end = pos + key.size();
        if ((pos == 0 || text[pos - 1] == '\n') && end < text.size() &&
            (text[end] == ' ' || key.back() == ':')) {
            const size_t num = text.find_first_not_of(' ', end);
            if (num == std::string_view::npos) {
                return false;
            }
            return std::from_chars(text.data() + num, text.data() + text.size(), out).ec ==
                   std::errc{};
        }
    }
    return false;
}

/// Path of this process's cgroup from /proc/self/cgroup: "0::/path" for v2, the line whose
/// controller list contains "memory" for v1.
std::string self_cgroup_path(const bool v2) {
    std::ifstream f("/proc/self/cgroup");
    std::string line;
    while (std::getline(f, line)) {
        const size_t c1 = line.find(':');
        const size_t c2 = c1 == std::string::npos ? c1 : line.find(':', c1 + 1);
        if (c2 == std::string::npos) {
            continue;
        }
        const std::string_view id    = std::string_view(line).substr(0, c1);
        const std::string_view ctrls = std::string_view(line).substr(c1 + 1, c2 - c1 - 1);
        const std::string_view path  = std::string_view(line).substr(c2 + 1);

        if (v2) {
            if (id == "0" && ctrls.empty()) {
                return std::string(path);
            }
            continue;
        }
        for (size_t p = 0; p <= ctrls.size();) {
            size_t comma = ctrls.find(',', p);
            if (comma == std::string_view::npos) {
                comma = ctrls.size();
            }
            if (ctrls.substr(p, comma - p) == "memory") {
                return std::string(path);
            }
            p = comma + 1;
        }
    }
    return {};
}

/// Try one cgroup hierarchy. Returns true and fills cg if usage can be read there.
bool try_hierarchy(CgroupMem &cg, const std::string &mount, const bool v2) {
    const char *usage_file = v2 ? "memory.current" : "memory.usage_in_bytes";
    const char *limit_file = v2 ? "memory.max" : "memory.limit_in_bytes";

    // Process's own cgroup dir; inside a container with a cgroup namespace or a bind mount the
    // path from /proc/self/cgroup may not exist, and the mount itself is the process's cgroup.
    std::string dir        = mount;
    const std::string path = self_cgroup_path(v2);
    if (!path.empty() && path != "/" &&
        ::access((mount + path + "/" + usage_file).c_str(), R_OK) == 0) {
        dir = mount + path;
    }

    cg.usage_fd = ::open((dir + "/" + usage_file).c_str(), O_RDONLY | O_CLOEXEC);
    if (cg.usage_fd < 0) {
        return false;
    }
    uint64_t probe;
    if (!pread_u64(cg.usage_fd, probe)) {
        ::close(cg.usage_fd);
        cg.usage_fd = -1;
        return false;
    }
    cg.stat_fd      = ::open((dir + "/memory.stat").c_str(), O_RDONLY | O_CLOEXEC);
    cg.inactive_key = v2 ? "inactive_file" : "total_inactive_file";

    // A parent's limit also applies: take the smallest one up to the mount point
    for (std::string d = dir;;) {
        uint64_t v;
        if (read_u64(d + "/" + limit_file, v)) {
            if (v >= kV1UnlimitedThreshold) {
                v = kUnlimited;
            }
            cg.limit = std::min(cg.limit, v);
        }
        if (d.size() <= mount.size()) {
            break;
        }
        d.resize(d.rfind('/'));
    }
    if (!v2 && cg.stat_fd >= 0) { // v1 already folds parent limits into this one
        char buf[8192];
        uint64_t h;
        if (find_key({buf, pread_all(cg.stat_fd, buf, sizeof(buf))}, "hierarchical_memory_limit",
                     h) &&
            h < kV1UnlimitedThreshold) {
            cg.limit = std::min(cg.limit, h);
        }
    }
    return true;
}

CgroupMem resolve_cgroup() {
    CgroupMem cg;
    const bool found = try_hierarchy(cg, "/sys/fs/cgroup", true) ||         // v2
                       try_hierarchy(cg, "/sys/fs/cgroup/unified", true) || // v2, hybrid layout
                       try_hierarchy(cg, "/sys/fs/cgroup/memory", false);   // v1
    if (!found || cg.limit == kUnlimited) { // nothing to cap against: don't pay for the reads
        if (cg.usage_fd >= 0) {
            ::close(cg.usage_fd);
        }
        if (cg.stat_fd >= 0) {
            ::close(cg.stat_fd);
        }
        cg = CgroupMem{};
    }
    return cg;
}

/// Resolved on first call (thread-safe static init). The limit is read once: a limit changed
/// while the process runs is not picked up.
const CgroupMem &cgroup() {
    static const CgroupMem cg = resolve_cgroup();
    return cg;
}

#endif // !CAPIO_MEMORY_CHECKS_NONE

/// @brief Compute the maximum allocable memory for a CapioFile heap buffer. this ensures that
/// requested memory for CapioFile buffers never exceeds available memory at allocation time
///
/// Available memory is the smaller of what the host has and what the process's cgroup (v1 or
/// v2) still allows, so containers and batch jobs with a memory limit are not over-allocated.
/// The cgroup path and limit are resolved once; each call only re-reads usage.
///
/// The check is selected at build time with CAPIO_MEMORY_CHECKS:
///   - full: host MemAvailable from /proc/meminfo, capped by cgroup limit - (usage - reclaimable
///           page cache). counts cache the kernel can drop as available
///   - fast: host free memory via sysconf, capped by cgroup limit - usage. no page cache
///           accounting, so it caps more aggressively than needed
///   - none: no check, the requested size is always returned
///
/// NOTE: MemAvailable in /proc/meminfo and inactive_file in memory.stat are the only places
///     reclaimable cache is exposed: no syscall reports it. all files are opened once and re-read
///     with pread, so a call costs at most three reads (meminfo, cgroup usage, memory.stat).
off64_t computeAllocableMemQuota(const off64_t requested_size) {
    START_LOG(gettid(), "call(requested_size = %lld)", static_cast<long long>(requested_size));

#if defined(CAPIO_MEMORY_CHECKS_NONE)
    LOG("Memory checks disabled for CapioFiles. returning requested size...");
    return requested_size;
#else
    const CgroupMem &cg  = cgroup();

#if defined(CAPIO_MEMORY_CHECKS_FAST)
    const long pages     = sysconf(_SC_AVPHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages < 0 || page_size < 0) {
        LOG("Unable to query free memory, using requested size");
        return requested_size;
    }
    uint64_t mem_available = static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
    LOG("Host free memory is: %llu", static_cast<unsigned long long>(mem_available));

    uint64_t usage;
    if (cg.active() && pread_u64(cg.usage_fd, usage)) {
        const uint64_t cg_available = cg.limit > usage ? cg.limit - usage : 0;
        LOG("cgroup limit %llu, usage %llu: %llu available",
            static_cast<unsigned long long>(cg.limit), static_cast<unsigned long long>(usage),
            static_cast<unsigned long long>(cg_available));
        mem_available = std::min(mem_available, cg_available);
    }
#else // CAPIO_MEMORY_CHECKS_FULL (default)
    static const int meminfo_fd = ::open("/proc/meminfo", O_RDONLY | O_CLOEXEC);

    char buf[8192]; // memory.stat is the largest read: ~1.5 KB on v2, ~1 KB on v1
    // MemAvailable is the third line: 256 bytes is enough
    const std::string_view meminfo(
        buf, static_cast<size_t>(std::max<ssize_t>(pread(meminfo_fd, buf, 256, 0), 0)));
    uint64_t kb = 0;
    if (!find_key(meminfo, "MemAvailable:", kb)) {
        LOG("MemAvailable not found in /proc/meminfo, using requested size");
        return requested_size;
    }
    uint64_t mem_available = kb * 1024;
    LOG("Host available memory is: %llu", static_cast<unsigned long long>(mem_available));

    uint64_t usage;
    if (cg.active() && pread_u64(cg.usage_fd, usage)) {
        uint64_t inactive = 0; // missing memory.stat: treat no cache as reclaimable
        if (cg.stat_fd >= 0) {
            find_key({buf, pread_all(cg.stat_fd, buf, sizeof(buf))}, cg.inactive_key, inactive);
        }
        const uint64_t working      = usage > inactive ? usage - inactive : 0;
        const uint64_t cg_available = cg.limit > working ? cg.limit - working : 0;
        LOG("cgroup limit %llu, working set %llu: %llu available",
            static_cast<unsigned long long>(cg.limit), static_cast<unsigned long long>(working),
            static_cast<unsigned long long>(cg_available));
        mem_available = std::min(mem_available, cg_available);
    }
#endif

    if (requested_size < 0 || mem_available >= static_cast<uint64_t>(requested_size)) {
        return requested_size;
    }
    const auto capped = static_cast<off64_t>(mem_available);

    LOG("Requested allocation memory exceeds available memory by %lld bytes. "
        "Capping allocation to %lld bytes.",
        static_cast<long long>(requested_size - capped), static_cast<long long>(capped));

    if (capped < requested_size / 10) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING,
                         "Available memory (%lld bytes) is less than 10%% of the requested "
                         "allocation (%lld bytes). Capping allocation to available memory.",
                         static_cast<long long>(capped), static_cast<long long>(requested_size));
    }

    return capped;
#endif
}

} // namespace

CapioFileMemoryAllocator::~CapioFileMemoryAllocator() { release(); }

char *CapioFileMemoryAllocator::data() const { return _data; }

off64_t CapioFileMemoryAllocator::size() const { return _size; }

bool CapioFileMemoryAllocator::empty() const { return _data == nullptr; }

bool CapioFileMemoryAllocator::allocateHeap(const off64_t size) {
    if (_data != nullptr) {
        return false;
    }
    _size = computeAllocableMemQuota(size);
    _data = new char[_size];
    return true;
}

bool CapioFileMemoryAllocator::allocateMapped(const std::filesystem::path &path,
                                              const off64_t size) {
    _size = computeAllocableMemQuota(size);
    _fd   = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
    if (_fd < 0 || ftruncate(_fd, _size) == -1) {
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
        return false;
    }
    _data = static_cast<char *>(mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0));
    if (_data == MAP_FAILED) {
        ::close(_fd);
        _fd   = -1;
        _data = nullptr;
        return false;
    }
    _mapped  = true;
    _map_len = _size;
    return true;
}

char *CapioFileMemoryAllocator::resize(
    const off64_t data_size, const std::vector<std::pair<off64_t, off64_t>> &sectors) {
    const off64_t new_size = std::max(data_size, computeAllocableMemQuota(_size * 2));
    char *const nb         = new char[new_size];
    for (const auto &[lbound, ubound] : sectors) {
        memcpy(nb + lbound, _data + lbound, ubound - lbound);
    }
    if (_mapped) {
        ::munmap(_data, _map_len);
    } else {
        delete[] _data;
    }
    _data    = nb;
    _size    = new_size;
    _mapped  = false;
    _map_len = 0;
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    return _data;
}

bool CapioFileMemoryAllocator::commit(const off64_t size) {
    if (ftruncate(_fd, size) == -1) {
        return false;
    }
    _size = size;
    if (::close(_fd) == -1) {
        return false;
    }
    _fd = -1;
    return true;
}

void CapioFileMemoryAllocator::release() {
    if (_data != nullptr) {
        if (_mapped) {
            ::munmap(_data, _map_len);
        } else {
            delete[] _data;
        }
        _data = nullptr;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
    _size    = 0;
    _mapped  = false;
    _map_len = 0;
}
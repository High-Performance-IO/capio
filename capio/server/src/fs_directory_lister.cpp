#include "storage/fs_directory_lister.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>

#include "calf/StlLogger.h"
#include "client-manager/client_manager.hpp"
#include "common/path.hpp"
#include "storage/fs_storage_manager.hpp"
#include "utils/capiocl_adapter.hpp"

extern ClientManager *client_manager;

namespace {
constexpr off64_t RECORD = sizeof(linux_dirent64);
} // namespace

FsDirectoryLister::FsDirectoryLister() {
    // Block the termination signals in the thread. Their handler frees memory and exits; if it ran
    // on a thread holding a lock (malloc's or CAPIO-CL's), it would wait for it forever.
    sigset_t signals, previous;
    sigemptyset(&signals);
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        sigaddset(&signals, signal);
    }
    pthread_sigmask(SIG_BLOCK, &signals, &previous);
    std::thread(&FsDirectoryLister::run, this).detach();
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

void FsDirectoryLister::list(Request request) {
    {
        const std::lock_guard lock(_mutex);
        _incoming.push_back(std::move(request));
    }
    _wake.notify_one();
}

void FsDirectoryLister::forget(const pid_t pid, const int fd) {
    {
        const std::lock_guard lock(_mutex);
        _forgotten.emplace_back(pid, fd);
    }
    _wake.notify_one();
}

void FsDirectoryLister::run() {
    for (;;) {
        std::vector<Request> incoming;
        std::vector<Key> forgotten;
        {
            std::unique_lock lock(_mutex);
            const auto has_news = [this] { return !_incoming.empty() || !_forgotten.empty(); };
            if (_pending.empty()) {
                _wake.wait(lock, has_news);
            } else {
                _wake.wait_for(lock, std::chrono::milliseconds(100), has_news);
            }
            incoming.swap(_incoming);
            forgotten.swap(_forgotten);
        }
        for (const auto &key : forgotten) {
            _listings.erase(key);
        }
        for (auto &request : incoming) {
            if (request.offset == 0) {
                // A new listing, or rewinddir: list the directory as it is now
                _listings.erase({request.pid, request.fd});
            }
            _pending.push_back(std::move(request));
        }
        _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                      [this](const Request &request) { return answer(request); }),
                       _pending.end());
    }
}

bool FsDirectoryLister::answer(const Request &request) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, offset=%ld, count=%ld)", request.tid,
              request.fd, request.path.c_str(), request.offset, request.count);
    Listing &listing   = _listings[{request.pid, request.fd}];
    const auto first   = static_cast<size_t>(request.offset / RECORD);
    const bool has_all = request.mode == Mode::AS_IS;

    if (first >= listing.entries.size() && !listing.is_complete) {
        const bool committed = has_all || isCommitted(request.path, request.expected);
        if (request.mode == Mode::AFTER_THE_COMMIT && !committed) {
            return false;
        }
        read(request.path, listing);
        listing.is_complete = committed; // read after the commit: nothing can be missing
        if (first >= listing.entries.size() && !listing.is_complete) {
            return false; // no new entry yet
        }
    }

    const size_t last =
        std::min(listing.entries.size(),
                 first + static_cast<size_t>(std::max<off64_t>(request.count, 0) / RECORD));
    std::vector<linux_dirent64> records(listing.entries.begin() + std::min(first, last),
                                        listing.entries.begin() + last);
    for (size_t i = 0; i < records.size(); ++i) {
        records[i].d_off = static_cast<off64_t>(first + i + 1) * RECORD;
    }
    const auto size = static_cast<off64_t>(records.size()) * RECORD;
    client_manager->replyToClientWithData(request.tid, request.offset + size,
                                          reinterpret_cast<const char *>(records.data()), size);
    return true;
}

void FsDirectoryLister::read(const std::string &path, Listing &listing) {
    const auto add = [&listing](const std::string &name, const std::filesystem::path &entry) {
        if (!listing.names.insert(name).second) {
            return;
        }
        linux_dirent64 record{};
        if (struct stat info{}; ::lstat(entry.c_str(), &info) == 0) {
            record.d_ino  = info.st_ino;
            record.d_type = IFTODT(info.st_mode);
        } else {
            record.d_type = DT_UNKNOWN;
        }
        record.d_reclen = RECORD;
        strncpy(record.d_name, name.c_str(), sizeof(record.d_name) - 1);
        listing.entries.push_back(record);
    };
    add(".", path);
    add("..", std::filesystem::path(path) / "..");
    std::error_code error;
    for (std::filesystem::directory_iterator entry(path, error), end; !error && entry != end;
         entry.increment(error)) {
        const std::string name = entry->path().filename();
        if (!is_forbidden_path(name)) { // CAPIO-CL's token files are not entries
            add(name, entry->path());
        }
    }
}

bool FsDirectoryLister::isCommitted(const std::string &path, const long expected) {
    const long count = FsStorageManager::countEntries(path);
    return count < 0 || // removed: nothing more will appear
           (count >= expected && FsStorageManager::isDirectoryCommitted(path, expected)) ||
           CapioCLEngine::get().isCommitted(path);
}

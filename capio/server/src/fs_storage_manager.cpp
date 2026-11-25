#include "storage/fs_storage_manager.hpp"

#include <algorithm>
#include <csignal>
#include <cstring>
#include <pthread.h>
#include <thread>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/path.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/filesystem.hpp"
#include "utils/runtime_configuration.hpp"

extern ClientManager *client_manager;

namespace {

/// @brief Erase the entries of @p map at or below @p path
template <typename Map> void erase_at_or_below(Map &map, const std::string &path) {
    for (auto entry = map.begin(); entry != map.end();) {
        entry = is_at_or_below(entry->first, path) ? map.erase(entry) : std::next(entry);
    }
}

/// @brief Rename the entries of @p map at or inside @p from, so that they are at or inside @p to
template <typename Map>
void move_at_or_below(Map &map, const std::string &from, const std::string &to) {
    Map moved;
    for (auto entry = map.begin(); entry != map.end();) {
        if (is_at_or_below(entry->first, from)) {
            moved.emplace(to + entry->first.substr(from.size()), std::move(entry->second));
            entry = map.erase(entry);
        } else {
            ++entry;
        }
    }
    map.merge(moved);
}

} // namespace

FsStorageManager::FsStorageManager() {
    loadExpectedEntries();
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_STATUS, "initialization completed.");
}

FsStorageManager::~FsStorageManager() {
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "teardown completed.");
}

bool FsStorageManager::isProducer(const pid_t tid, const std::filesystem::path &path) {
    return CapioCLEngine::get().isProducer(path, client_manager->getAppName(tid)) ||
           client_manager->isProducer(tid, path);
}

bool FsStorageManager::waitsForCreation(const pid_t tid, const std::filesystem::path &path) {
    if (isProducer(tid, path) || CapioCLEngine::get().isExcluded(path)) {
        return false;
    }
    return !RuntimeConfiguration::get().hasConfig() ||
           CapioCLEngine::get().isConsumer(path, client_manager->getAppName(tid));
}

bool FsStorageManager::isDeclared(const std::filesystem::path &path) {
    return !RuntimeConfiguration::get().hasConfig() ||
           !CapioCLEngine::get().getConsumers(path).empty() ||
           !CapioCLEngine::get().getProducers(path).empty();
}

void FsStorageManager::commit(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    if (_failed_closes.count(path) == 0) {
        CapioCLEngine::get().setCommitted(path);
    }
}

void FsStorageManager::applyOnClose(const std::filesystem::path &path, const long n_closes) {
    const auto &engine = CapioCLEngine::get();
    if (_failed_closes.count(path) != 0 ||
        engine.getCommitRule(path) != capiocl::commitRules::ON_CLOSE) {
        return;
    }
    if (engine.getCommitCloseCount(path) <= 1) {
        commit(path); // what increaseCloseCount does, without asking first
        return;
    }
    // Counting asks the other nodes, so it runs on its own thread; the server waits for it before
    // it exits. The thread does not get the termination signals: their handler frees memory and
    // exits, and run on a thread that holds a lock it would wait for it forever.
    // TODO: a thread manager for the threads of the server
    ++_pending_counts;
    sigset_t signals, previous;
    sigemptyset(&signals);
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        sigaddset(&signals, signal);
    }
    pthread_sigmask(SIG_BLOCK, &signals, &previous);
    std::thread t([this, path, n_closes] {
        try {
            for (long i = 0; i < n_closes; ++i) {
                if (CapioCLEngine::get().increaseCloseCount(path)) {
                    break;
                }
            }
        } catch (const std::exception &exception) {
            CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Could not count a close of %s: %s",
                             path.c_str(), exception.what());
        }
        --_pending_counts;
    });
    t.detach();
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
}

void FsStorageManager::countClose(const std::filesystem::path &path) {
    ++_close_counts[path];
    applyOnClose(path, 1);
}

void FsStorageManager::closed(const pid_t tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());
    if (!CapioCLEngine::get().isExcluded(path) && isProducer(tid, path)) {
        countClose(path);
    }
}

void FsStorageManager::addFileToPid(const pid_t pid, const int fd,
                                    const std::filesystem::path &path) {
    START_LOG(gettid(), "call(pid=%d, fd=%d, path=%s)", pid, fd, path.c_str());
    _opened_fd_map[pid][fd] = path;
}

void FsStorageManager::dup(const pid_t pid, const int old_fd, const int new_fd) {
    START_LOG(gettid(), "call(pid=%d, old_fd=%d, new_fd=%d)", pid, old_fd, new_fd);
    if (const auto path = getPath(pid, old_fd); !path.empty() && old_fd != new_fd) {
        _opened_fd_map[pid][new_fd] = path;
    }
}

std::string FsStorageManager::getPath(const pid_t pid, const int fd) const {
    const auto process = _opened_fd_map.find(pid);
    if (process == _opened_fd_map.end()) {
        return {};
    }
    const auto descriptor = process->second.find(fd);
    return descriptor == process->second.end() ? std::string{} : descriptor->second;
}

void FsStorageManager::closed(const pid_t tid, const pid_t pid, const int fd, const int error) {
    START_LOG(gettid(), "call(tid=%d, pid=%d, fd=%d, error=%d)", tid, pid, fd, error);
    const auto process = _opened_fd_map.find(pid);
    if (process == _opened_fd_map.end()) {
        return;
    }
    const auto descriptor = process->second.find(fd);
    if (descriptor == process->second.end()) {
        LOG("Descriptor %d of process %d is not known", fd, pid);
        return;
    }
    const std::filesystem::path path = descriptor->second;
    process->second.erase(descriptor);
    if (process->second.empty()) {
        _opened_fd_map.erase(process);
    }
    if (error != 0) {
        closeFailed(tid, path, error);
    } else {
        closed(tid, path);
    }
}

void FsStorageManager::closeFailed(const pid_t tid, const std::filesystem::path &path,
                                   const int error) {
    _failed_closes[path] = error;
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                     "Closing %s failed in thread %d (%s): data was lost, so the file will not be "
                     "committed and its consumers will wait",
                     path.c_str(), tid, strerror(error));
}

void FsStorageManager::forgetHistory(const std::string &path) {
    erase_at_or_below(_close_counts, path);
    erase_at_or_below(_failed_closes, path);
    erase_at_or_below(_new_entries, path);
    for (auto &[pid, descriptors] : _opened_fd_map) {
        for (auto descriptor = descriptors.begin(); descriptor != descriptors.end();) {
            descriptor = is_at_or_below(descriptor->second, path) ? descriptors.erase(descriptor)
                                                                  : std::next(descriptor);
        }
    }
    client_manager->forgetProducedFiles(path);
}

void FsStorageManager::moveHistory(const std::string &from, const std::string &to) {
    move_at_or_below(_close_counts, from, to);
    move_at_or_below(_failed_closes, from, to);
    move_at_or_below(_new_entries, from, to);
    for (auto &[pid, descriptors] : _opened_fd_map) {
        for (auto &[fd, path] : descriptors) {
            if (is_at_or_below(path, from)) {
                path = to + path.substr(from.size());
            }
        }
    }
    client_manager->renameProducedFiles(from, to);
}

void FsStorageManager::rename(const pid_t tid, const std::string &from, const bool from_capio,
                              const std::string &to, const bool to_capio) {
    START_LOG(gettid(), "call(tid=%d, from=%s, to=%s)", tid, from.c_str(), to.c_str());
    // The rename replaces whatever was at the new name, so forget it. Then move the old name's
    // history to the new name, or forget it if the file has left CAPIO_DIR.
    if (to_capio) {
        forgetHistory(to);
    }
    if (from_capio) {
        to_capio ? moveHistory(from, to) : forgetHistory(from);
    }
    if (!to_capio || CapioCLEngine::get().isExcluded(to)) {
        return;
    }
    // The new name is a new entry of its directory, and so is everything inside it if it is a
    // directory. Count them for on_n_files directories.
    noteNewEntry(to);
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(to, error);
    if (is_directory) {
        for (std::filesystem::recursive_directory_iterator entry(to, error), end;
             !error && entry != end; entry.increment(error)) {
            noteNewEntry(entry->path());
        }
    }
    if (!from_capio) {
        // CAPIO never saw this file being written, so count the move into CAPIO_DIR as its close
        client_manager->registerProducedFile(tid, to);
        countClose(to);
        return;
    }
    if (!client_manager->hasProducer(to) && !is_directory) {
        // Nobody produces the file any more (e.g. mv after the producer exited): the renaming
        // process becomes its producer, so the file is committed when it exits
        client_manager->registerProducedFile(tid, to);
    }
    // Apply the commit rule of each new name, using the closes counted under the old one
    for (const auto &[path, count] : _close_counts) {
        if (count > 0 && is_at_or_below(path, to)) {
            applyOnClose(path, count);
        }
    }
}

void FsStorageManager::loadExpectedEntries() {
    const auto &engine = CapioCLEngine::get();
    for (const auto &path : engine.getPaths()) {
        if (engine.getCommitRule(path) == capiocl::commitRules::ON_N_FILES) {
            _expected_entries[std::filesystem::path(path).lexically_normal()] =
                std::max(1L, engine.getDirectoryFileCount(path));
        }
    }
}

long FsStorageManager::expectedEntries(const std::filesystem::path &directory) {
    if (const auto known = _expected_entries.find(directory); known != _expected_entries.end()) {
        return known->second;
    }
    // Not in the config at startup (it matched a glob, or came from the dynamic API): use what
    // CAPIO-CL reports the first time we ask
    const long expected = std::max(1L, CapioCLEngine::get().getDirectoryFileCount(directory));
    _expected_entries[directory] = expected;
    return expected;
}

long FsStorageManager::countEntries(const std::filesystem::path &directory) {
    std::error_code error;
    std::filesystem::directory_iterator entry(directory, error);
    long count = 0;
    while (!error && entry != std::filesystem::directory_iterator()) {
        count += !is_forbidden_path(entry->path().filename().native());
        entry.increment(error);
    }
    return error ? -1 : count;
}

bool FsStorageManager::isDirectoryCommitted(const std::filesystem::path &directory,
                                            const long n_expected) {
    const auto &engine = CapioCLEngine::get();
    if (countEntries(directory) >= n_expected) {
        engine.setCommitted(directory);
        return true;
    }
    return engine.isCommitted(directory);
}

void FsStorageManager::noteNewEntry(const std::filesystem::path &path) {
    const auto directory = path.parent_path();
    if (_committed_directories.count(directory) != 0 ||
        CapioCLEngine::get().getCommitRule(directory) != capiocl::commitRules::ON_N_FILES) {
        return;
    }
    auto &created = _new_entries[directory];
    created.insert(path.filename());
    const long expected = expectedEntries(directory);
    if (static_cast<long>(created.size()) >= expected && countEntries(directory) >= expected) {
        commit(directory);
        _committed_directories.insert(directory);
        _new_entries.erase(directory);
    }
}

void FsStorageManager::waitForCreation(const std::filesystem::path &path) const {
    std::error_code error;
    while (!std::filesystem::exists(path, error)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::optional<off64_t> FsStorageManager::readable(const std::filesystem::path &path,
                                                  const off64_t end_of_read) {
    if (!CapioCLEngine::get().isFirable(path)) {
        return std::nullopt;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < static_cast<uintmax_t>(end_of_read)) {
        return std::nullopt;
    }
    return static_cast<off64_t>(size);
}

off64_t FsStorageManager::waitForData(const std::filesystem::path &path,
                                      const off64_t end_of_read) const {
    for (;;) {
        if (const auto size = readable(path, end_of_read)) {
            return *size;
        }
        if (CapioCLEngine::get().isCommitted(path)) {
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

off64_t FsStorageManager::waitForConsent(const std::filesystem::path &path, const bool by_entries,
                                         const long expected) const {
    for (;;) {
        if (std::error_code error; std::filesystem::exists(path, error)) {
            // Directories are never committed, except those with an on_n_files rule
            if ((std::filesystem::is_directory(path, error) &&
                 CapioCLEngine::get().getCommitRule(path) != capiocl::commitRules::ON_N_FILES) ||
                !isDeclared(path)) {
                return 0;
            }
            if (CapioCLEngine::get().isFirable(path) ||
                (by_entries ? isDirectoryCommitted(path, expected)
                            : CapioCLEngine::get().isCommitted(path))) {
                return 1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void FsStorageManager::waitForPendingCounts(const std::chrono::milliseconds timeout) const {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (_pending_counts > 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

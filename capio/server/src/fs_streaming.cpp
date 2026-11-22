#include "fs-streaming/fs_streaming.hpp"

#include <cstring>
#include <thread>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/constants.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/runtime_configuration.hpp"

extern ClientManager *client_manager;

FsStreaming &FsStreaming::get() {
    static FsStreaming instance;
    return instance;
}

void FsStreaming::addThread(const pid_t tid, const pid_t pid, const std::string &app) {
    START_LOG(gettid(), "call(tid=%d, pid=%d, app=%s)", tid, pid, app.c_str());
    if (const auto known = threads.find(tid); known != threads.end() && known->second.pid == pid) {
        // execve: the same process, running another program. It worked, so what it was to close
        // is closed, and counts as closed by the program that execed.
        if (const auto pending = closes_at_exec.find(known->second.process);
            pending != closes_at_exec.end()) {
            const auto closes = std::move(pending->second);
            closes_at_exec.erase(pending);
            for (const auto &[path, writable] : closes) {
                closed(tid, path, CloseKind::IMPLICIT, writable);
            }
        }
        known->second.app = app;
        return;
    }
    if (tid == pid || processes.find(pid) == processes.end()) {
        processes[pid] = ++last_process;
    }
    threads[tid] = {pid, processes[pid], app};
}

void FsStreaming::removeThread(const pid_t tid) {
    START_LOG(gettid(), "call(tid=%d)", tid);
    threads.erase(tid);
    path_parts.erase(tid);
    renaming.erase(tid);
}

/*
 * An app is all the processes with its CAPIO_APP_NAME. Without a name they would all be the same
 * app, default_app, and consumers would count as producers of what they wait for: each process is
 * then on its own.
 */
std::string FsStreaming::producerName(const pid_t tid) const {
    const auto thread = threads.find(tid);
    if (thread == threads.end()) {
        return "thread " + std::to_string(tid);
    }
    return thread->second.app == CAPIO_DEFAULT_APP_NAME
               ? "process " + std::to_string(thread->second.process)
               : "app " + thread->second.app;
}

void FsStreaming::addProducer(const pid_t tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());
    producers[path].insert(producerName(tid));
    producer_threads[path].insert(tid);
}

bool FsStreaming::isProducer(const pid_t tid, const std::filesystem::path &path) const {
    if (const auto thread = threads.find(tid);
        thread != threads.end() && CapioCLEngine::get().isProducer(path, thread->second.app)) {
        return true;
    }
    const auto found = producers.find(path);
    return found != producers.end() && found->second.count(producerName(tid)) != 0;
}

void FsStreaming::addOpener(const pid_t tid, const std::filesystem::path &path) {
    if (const auto thread = threads.find(tid); thread != threads.end()) {
        openers[path].insert(thread->second.process);
    }
}

void FsStreaming::closed(const pid_t tid, const std::filesystem::path &path, const CloseKind kind,
                         const bool writable) {
    START_LOG(gettid(), "call(tid=%d, path=%s, implicit=%d)", tid, path.c_str(),
              kind == CloseKind::IMPLICIT);
    const auto thread = threads.find(tid);
    if (thread == threads.end() || CapioCLEngine::get().isExcluded(path)) {
        return;
    }
    if (kind == CloseKind::IMPLICIT) {
        // A child gets a copy of every descriptor. The kernel closing the copy when the child
        // exits or execs must not commit a file its parent is still writing.
        const auto opened_by = openers.find(path);
        if (opened_by == openers.end() || opened_by->second.count(thread->second.process) == 0) {
            LOG("Implicit close of an inherited copy: not counted");
            return;
        }
    }
    // Without an app name an inherited descriptor cannot be traced to its producer: who writes
    // the file produces it.
    const bool unnamed_writer = writable && thread->second.app == CAPIO_DEFAULT_APP_NAME;
    if (unnamed_writer || isProducer(tid, path)) {
        countClose(path);
    }
}

void FsStreaming::closeFailed(const pid_t tid, const std::filesystem::path &path, const int error) {
    START_LOG(gettid(), "call(tid=%d, path=%s, error=%d)", tid, path.c_str(), error);
    failed[path] = error;
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR,
                     "Closing %s failed in thread %d (%s): data was lost, so the file will not be "
                     "committed and its consumers will wait",
                     path.c_str(), tid, strerror(error));
}

void FsStreaming::closeAtExec(const pid_t tid, const std::filesystem::path &path,
                              const bool writable) {
    if (const auto thread = threads.find(tid); thread != threads.end()) {
        closes_at_exec[thread->second.process].emplace_back(path, writable);
    }
}

void FsStreaming::execFailed(const pid_t tid) {
    if (const auto thread = threads.find(tid); thread != threads.end()) {
        closes_at_exec.erase(thread->second.process);
    }
}

std::vector<pid_t> FsStreaming::otherThreads(const pid_t tid) const {
    std::vector<pid_t> others;
    if (const auto self = threads.find(tid); self != threads.end()) {
        for (const auto &[other, thread] : threads) {
            if (other != tid && thread.process == self->second.process) {
                others.push_back(other);
            }
        }
    }
    return others;
}

namespace {

/// @brief Whether @p path is @p name or inside the directory @p name
bool is_at_or_below(const std::string &path, const std::string &name) {
    return path.compare(0, name.size(), name) == 0 &&
           (path.size() == name.size() || path[name.size()] == '/');
}

/// @brief Move the entries at or below @p from to @p to, replacing those that were there
template <typename Map>
void move_entries(Map &map, const std::string &from, const std::string &to, const bool keep) {
    Map moved;
    for (auto entry = map.begin(); entry != map.end();) {
        if (is_at_or_below(entry->first, from)) {
            if (keep) {
                moved.emplace(to + entry->first.substr(from.size()), std::move(entry->second));
            }
            entry = map.erase(entry);
        } else if (keep && is_at_or_below(entry->first, to)) {
            entry = map.erase(entry); // the file that the rename replaced
        } else {
            ++entry;
        }
    }
    for (auto &[path, value] : moved) {
        map[path] = std::move(value);
    }
}

} // namespace

void FsStreaming::renameFrom(const pid_t tid, std::string path, const bool in_capio_dir) {
    renaming[tid] = {std::move(path), in_capio_dir};
}

void FsStreaming::renameTo(const pid_t tid, const std::string &to, const bool to_capio) {
    const auto half = renaming.find(tid);
    if (half == renaming.end()) {
        return;
    }
    const auto [from, from_capio] = half->second;
    renaming.erase(half);
    START_LOG(gettid(), "call(tid=%d, from=%s, to=%s)", tid, from.c_str(), to.c_str());

    if (!from_capio) {
        if (!to_capio) {
            return;
        }
        // CAPIO never saw it being written: moving it in is its one close
        failed.erase(to);
        closes[to]           = 0;
        producer_threads[to] = {};
        producers[to]        = {};
        if (!CapioCLEngine::get().isExcluded(to)) {
            addProducer(tid, to);
            client_manager->registerProducedFile(tid, to);
            countClose(to);
        }
        return;
    }

    // Who commits it on_termination: the threads that produced it, under the new name
    std::vector<std::string> renamed;
    for (const auto &[path, producing] : producer_threads) {
        if (!is_at_or_below(path, from)) {
            continue;
        }
        const auto new_path = to + path.substr(from.size());
        bool one_is_alive   = false;
        for (const auto thread : producing) {
            if (threads.count(thread) == 0) {
                continue;
            }
            client_manager->removeProducedFile(thread, path);
            if (to_capio) {
                client_manager->registerProducedFile(thread, new_path);
                one_is_alive = true;
            }
        }
        if (to_capio) {
            renamed.push_back(new_path);
            if (!one_is_alive) { // mv, after the producer ended: it commits when mv ends
                client_manager->registerProducedFile(tid, new_path);
            }
        }
    }

    move_entries(producers, from, to, to_capio);
    move_entries(producer_threads, from, to, to_capio);
    move_entries(openers, from, to, to_capio);
    move_entries(closes, from, to, to_capio);
    move_entries(failed, from, to, to_capio);

    for (const auto &path : renamed) {
        producers[path].insert(producerName(tid));
        producer_threads[path].insert(tid);
    }
    if (to_capio) {
        // The rule of each new name, with the history of its file
        for (const auto &[path, n_closes] : closes) {
            if (n_closes > 0 && is_at_or_below(path, to)) {
                applyOnClose(path, n_closes);
            }
        }
    }
}

void FsStreaming::addPathPart(const pid_t tid, const char *part) { path_parts[tid] += part; }

std::string FsStreaming::takePath(const pid_t tid, const char *tail) {
    std::string path;
    if (const auto parts = path_parts.find(tid); parts != path_parts.end()) {
        path = std::move(parts->second);
        path_parts.erase(parts);
    }
    return path + tail;
}

/*
 * Nothing here may ask whether a file is committed: CAPIO-CL's multicast monitor sleeps 300 ms
 * before it answers no, and the request thread serves every client of the node. Committing only
 * sends a message. Questions are asked by waiter and background threads.
 */
void FsStreaming::commit(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    if (failed.count(path) != 0) {
        return;
    }
    CapioCLEngine::get().setCommitted(path);
}

void FsStreaming::countClose(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    ++closes[path];
    applyOnClose(path, 1);
}

void FsStreaming::applyOnClose(const std::filesystem::path &path, const long n_closes) {
    const auto &engine = CapioCLEngine::get();
    if (failed.count(path) != 0 || engine.getCommitRule(path) != capiocl::commitRules::ON_CLOSE) {
        return;
    }
    if (engine.getCommitCloseCount(path) <= 1) {
        commit(path); // what increaseCloseCount does, without asking first
        return;
    }
    // Counting asks the other nodes. The order of the counts does not matter, and the client that
    // closed does not need to be alive.
    std::thread([path, n_closes] {
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
    }).detach();
}

bool FsStreaming::canWaitForCommit(const std::filesystem::path &path) {
    if (path == RuntimeConfiguration::get().getCapioDir()) {
        return false; // CAPIO_DIR is the working directory, not an output
    }
    std::error_code error;
    return !std::filesystem::is_directory(path, error) ||
           CapioCLEngine::get().getCommitRule(path) == capiocl::commitRules::ON_N_FILES;
}

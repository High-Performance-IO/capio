#include "fs-streaming/fs_streaming.hpp"

#include <thread>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "common/constants.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/runtime_configuration.hpp"

FsStreaming &FsStreaming::get() {
    static FsStreaming instance;
    return instance;
}

void FsStreaming::addThread(const pid_t tid, const pid_t pid, const std::string &app) {
    START_LOG(gettid(), "call(tid=%d, pid=%d, app=%s)", tid, pid, app.c_str());
    if (const auto known = threads.find(tid); known != threads.end() && known->second.pid == pid) {
        known->second.app = app; // execve: the same process, running another program
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
}

bool FsStreaming::isProducer(const pid_t tid, const std::filesystem::path &path) const {
    if (const auto thread = threads.find(tid);
        thread != threads.end() && CapioCLEngine::get().isProducer(path, thread->second.app)) {
        return true;
    }
    const auto found = producers.find(path);
    return found != producers.end() && found->second.count(producerName(tid)) != 0;
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
    CapioCLEngine::get().setCommitted(path);
}

void FsStreaming::countClose(const std::filesystem::path &path) {
    START_LOG(gettid(), "call(path=%s)", path.c_str());
    const auto &engine = CapioCLEngine::get();
    if (engine.getCommitRule(path) != capiocl::commitRules::ON_CLOSE) {
        return;
    }
    if (engine.getCommitCloseCount(path) <= 1) {
        commit(path); // what increaseCloseCount does, without asking first
        return;
    }
    // Counting asks the other nodes. The order of the counts does not matter, and the client that
    // closed does not need to be alive.
    std::thread([path] {
        try {
            CapioCLEngine::get().increaseCloseCount(path);
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

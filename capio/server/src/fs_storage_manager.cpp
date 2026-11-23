#include "storage/fs_storage_manager.hpp"

#include <algorithm>
#include <csignal>
#include <pthread.h>
#include <thread>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"
#include "capiocl.hpp"
#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/runtime_configuration.hpp"

extern ClientManager *client_manager;

FsStorageManager::FsStorageManager() {
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
    CapioCLEngine::get().setCommitted(path);
}

void FsStorageManager::applyOnClose(const std::filesystem::path &path, const long n_closes) {
    const auto &engine = CapioCLEngine::get();
    if (engine.getCommitRule(path) != capiocl::commitRules::ON_CLOSE) {
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

void FsStorageManager::countClose(const std::filesystem::path &path) { applyOnClose(path, 1); }

void FsStorageManager::closed(const pid_t tid, const std::filesystem::path &path) {
    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path.c_str());
    if (!CapioCLEngine::get().isExcluded(path) && isProducer(tid, path)) {
        countClose(path);
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

off64_t FsStorageManager::waitForConsent(const std::filesystem::path &path) const {
    for (;;) {
        if (std::error_code error; std::filesystem::exists(path, error)) {
            // Directories are never committed
            if (std::filesystem::is_directory(path, error) || !isDeclared(path)) {
                return 0;
            }
            if (CapioCLEngine::get().isFirable(path) || CapioCLEngine::get().isCommitted(path)) {
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

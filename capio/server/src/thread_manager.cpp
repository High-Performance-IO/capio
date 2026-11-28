#include "utils/thread_manager.hpp"

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

std::uint64_t ThreadManager::enter(const Kind kind, std::string name) {
    const std::lock_guard lock(_mutex);
    if (_stopped) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "Shutting down: not starting %s", name.c_str());
        return 0;
    }
    const std::uint64_t id = _next_id++;
    _running.emplace(id, Running{kind, std::move(name), std::chrono::steady_clock::now()});
    if (kind == Kind::TASK) {
        ++_tasks;
    }
    return id;
}

void ThreadManager::leave(const std::uint64_t id) {
    {
        const std::lock_guard lock(_mutex);
        const auto it = _running.find(id);
        if (it == _running.end()) {
            return;
        }
        if (it->second.kind == Kind::TASK) {
            --_tasks;
        }
        _running.erase(it);
    }
    _task_ended.notify_all();
}

ThreadManager::Leave::~Leave() { manager.leave(id); }

bool ThreadManager::stop(const std::chrono::milliseconds timeout) {
    std::unique_lock lock(_mutex);
    _stopped       = true;
    // the calling thread, if it is a task, cannot end while it waits here
    const int self = _in_task ? 1 : 0;
    const bool finished =
        _task_ended.wait_for(lock, timeout, [this, self] { return _tasks <= self; });

    const auto now = std::chrono::steady_clock::now();
    for (const auto &[id, thread] : _running) {
        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(now - thread.since).count();
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "%s still running, for %llds: %s",
                         thread.kind == Kind::TASK ? "Task" : "Waiter",
                         static_cast<long long>(seconds), thread.name.c_str());
    }
    return finished;
}

std::vector<ThreadManager::Running> ThreadManager::running() const {
    const std::lock_guard lock(_mutex);
    std::vector<Running> threads;
    threads.reserve(_running.size());
    for (const auto &[id, thread] : _running) {
        threads.push_back(thread);
    }
    return threads;
}

ThreadManager &thread_manager() {
    static auto *const manager = new ThreadManager();
    return *manager;
}

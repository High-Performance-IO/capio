#ifndef CAPIO_SERVER_UTILS_THREAD_MANAGER_HPP
#define CAPIO_SERVER_UTILS_THREAD_MANAGER_HPP

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief Masks the termination signals (SIGTERM, SIGINT, SIGQUIT, SIGPIPE) in the calling thread
 * for as long as the object lives, so that the threads started in its scope never receive them.
 *
 * Why: the kernel delivers a signal sent to the process to any one thread that does not mask it.
 * The handler of these signals (sig_term_handler) frees the managers of the server: run on a thread
 * in the middle of a request it would free what that request is using, and run on a thread that
 * holds a lock (malloc's, CAPIO-CL's, a CapioFile's) it would wait for that lock forever. Masked in
 * every other thread, the signals can only reach the main thread, which just joins the others.
 *
 * How the mask reaches the new threads: the object itself is never passed to them. The signal mask
 * is an attribute of each thread, and a new thread starts with a copy of the mask of the thread
 * that creates it (pthread_create, under std::thread, is specified so by POSIX). So:
 *   - the constructor masks the signals in the calling thread, and keeps its previous mask;
 *   - each std::thread started while the object lives is born with the signals masked, before it
 *     runs any of its code, and keeps them masked for its whole life;
 *   - the destructor gives the calling thread its previous mask back. It changes only the mask of
 *     the calling thread: the threads already started keep their own copy.
 *
 * Usage:
 * @code
 * {
 *     const TerminationSignalMasker masker;
 *     std::thread worker(body); // worker never receives SIGTERM, SIGINT, SIGQUIT or SIGPIPE
 * }                             // the calling thread receives them again
 * @endcode
 */
class TerminationSignalMasker {
    /// Mask of the calling thread before the constructor, given back by the destructor
    sigset_t _previous{};

  public:
    TerminationSignalMasker() {
        sigset_t signals;
        sigemptyset(&signals);
        for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
            sigaddset(&signals, signal);
        }
        // adds the signals to the mask of the calling thread only (not of the whole process)
        pthread_sigmask(SIG_BLOCK, &signals, &_previous);
    }

    /// Also runs if std::thread throws in the scope: the caller never stays masked by mistake
    ~TerminationSignalMasker() { pthread_sigmask(SIG_SETMASK, &_previous, nullptr); }

    TerminationSignalMasker(const TerminationSignalMasker &)            = delete;
    TerminationSignalMasker &operator=(const TerminationSignalMasker &) = delete;
};

/**
 * @brief Owns the threads the server starts besides its two main ones (the request loop and the
 * remote listener): every one of them starts here.
 *
 * A thread is of one of two kinds:
 *   - a waiter waits for something (a file, its data, its commit, the home of a file) before it
 *     answers a client. What it waits for may never come (a file nobody produces), so the shutdown
 *     does not wait for it;
 *   - a task is work that must be done before the server exits (counting the closes of a file:
 *     lost, it would leave the file uncommitted for the servers that start later), so the shutdown
 *     waits for it, for a bounded time.
 *
 * Each thread starts inside a TerminationSignalMasker, so it never runs the handler of the
 * termination signals. Each has a name, only for the log: stop() prints the names of the threads
 * still running, to tell which wait is stuck.
 *
 * One thread per wait, not a pool: a waiter can wait forever, so a bounded pool would make the
 * other waits queue behind it, and hang clients that have nothing to do with it.
 */
class ThreadManager {
  public:
    enum class Kind { WAITER, TASK };

    /// @brief A thread running, as stop() logs it
    struct Running {
        Kind kind;
        std::string name; ///< only for the log: see thread_name()
        std::chrono::steady_clock::time_point since;
    };

    ThreadManager()                                 = default;
    ThreadManager(const ThreadManager &)            = delete;
    ThreadManager &operator=(const ThreadManager &) = delete;

    /**
     * @brief Run @p body(@p args...) on a thread that waits for something before it answers a
     * client.
     * @param name the name of the thread, only for the log of stop(): see thread_name()
     * @return false if the manager is stopped, and the thread is not started
     */
    template <typename Body, typename... Args>
    bool startWaiter(std::string name, Body &&body, Args &&...args) {
        return start(Kind::WAITER, std::move(name), std::forward<Body>(body),
                     std::forward<Args>(args)...);
    }

    /**
     * @brief Run @p body(@p args...) on a thread doing work that must be done before the server
     * exits: stop() waits for it.
     * @param name the name of the thread, only for the log of stop()
     * @return false if the manager is stopped, and the thread is not started
     */
    template <typename Body, typename... Args>
    bool startTask(std::string name, Body &&body, Args &&...args) {
        return start(Kind::TASK, std::move(name), std::forward<Body>(body),
                     std::forward<Args>(args)...);
    }

    /**
     * @brief For the shutdown: start no more threads, wait at most @p timeout for the tasks
     * running, then log every thread still running. The waiters are not waited for. Called from a
     * thread of the manager (a fault in it), that thread is not waited for.
     * @return whether every task finished
     */
    bool stop(std::chrono::milliseconds timeout);

    /// @brief The threads running now, oldest first
    [[nodiscard]] std::vector<Running> running() const;

  private:
    mutable std::mutex _mutex;
    std::condition_variable _task_ended;
    std::map<std::uint64_t, Running> _running;
    std::uint64_t _next_id = 1;
    int _tasks             = 0;
    bool _stopped          = false;

    /// @brief Whether the calling thread is a task of a manager: stop() does not wait for itself
    static inline thread_local bool _in_task = false;

    /// @brief Register a thread about to start. @return its id, or 0 if the manager is stopped
    std::uint64_t enter(Kind kind, std::string name);

    /// @brief Forget the thread @p id: it ended, or it could not start
    void leave(std::uint64_t id);

    template <typename Body, typename... Args>
    bool start(const Kind kind, std::string name, Body &&body, Args &&...args) {
        const std::uint64_t id = enter(kind, std::move(name));
        if (id == 0) {
            return false;
        }
        try {
            const TerminationSignalMasker masker; // the thread below inherits the masked signals
            // As std::thread does, the body and the arguments are copied (or moved) into the
            // thread, and std::ref passes a reference
            std::thread([this, id, kind, body = std::decay_t<Body>(std::forward<Body>(body)),
                         arguments = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                _in_task = kind == Kind::TASK;
                const Leave leave{*this, id};
                std::apply(std::move(body), std::move(arguments));
            }).detach();
        } catch (...) {
            leave(id);
            throw;
        }
        return true;
    }

    /// @brief Forgets the thread when it ends, also by an exception
    struct Leave {
        ThreadManager &manager;
        std::uint64_t id;
        ~Leave();
    };
};

/**
 * @brief The name of a waiter, which ThreadManager::stop() prints if the thread is still running at
 * shutdown. Only a name: nothing else reads it. All the waiters are named so, to read alike in the
 * log: "<request> of <path> by <client>: <what it waits for>", e.g.
 * "open of /a/b by 1234: its creation".
 */
inline std::string thread_name(const char *request, const std::filesystem::path &path,
                               const pid_t client, const char *waits_for) {
    return std::string(request) + " of " + path.string() + " by " + std::to_string(client) + ": " +
           waits_for;
}

/// @brief As thread_name() for a waiter of a request of the server @p server: "<request> of <path>
/// for <server>: <what it waits for>"
inline std::string thread_name(const char *request, const std::filesystem::path &path,
                               const std::string &server, const char *waits_for) {
    return std::string(request) + " of " + path.string() + " for " + server + ": " + waits_for;
}

/// @brief The thread manager of the server. Never destroyed: the threads it owns are detached, and
/// may still run while the process exits.
ThreadManager &thread_manager();

#endif // CAPIO_SERVER_UTILS_THREAD_MANAGER_HPP

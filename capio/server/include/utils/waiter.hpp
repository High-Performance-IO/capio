#ifndef CAPIO_SERVER_UTILS_WAITER_HPP
#define CAPIO_SERVER_UTILS_WAITER_HPP

#include <csignal>
#include <pthread.h>
#include <thread>
#include <utility>

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
 * @brief Run @p body(@p args...) on a detached thread that waits for something (a file, its data,
 * its commit, another server) before it answers a client. The thread is started inside a
 * TerminationSignalMasker, so it inherits a mask with the termination signals masked and never
 * runs their handler; the mask of the caller is given back when spawn_waiter returns, also if the
 * thread cannot be started.
 *
 * Every waiting thread of the server starts here, so that a thread manager (#327) can replace it
 * without touching the handlers.
 */
template <typename Body, typename... Args> void spawn_waiter(Body &&body, Args &&...args) {
    const TerminationSignalMasker masker; // the thread below inherits the masked signals
    std::thread(std::forward<Body>(body), std::forward<Args>(args)...).detach();
}

#endif // CAPIO_SERVER_UTILS_WAITER_HPP

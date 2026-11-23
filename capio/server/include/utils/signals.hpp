#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <atomic>
#include <csignal>
#include <cstdlib>

#include <unistd.h>

#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"

#include "remote/backend.hpp"
#include "remote/discovery.hpp"

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

/// Thread running the server teardown, or 0 while no teardown has started.
inline std::atomic<pid_t> shutdown_thread{0};

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    // The teardown below frees the global managers, so it must run exactly once. Signals are
    // delivered to any thread that does not block them: a second SIGTERM (e.g. a launcher and a
    // cleanup script both terminating the server), or a fault in a worker thread using a manager
    // that is being freed, would otherwise run a second, concurrent teardown (double free).
    if (pid_t expected = 0; !shutdown_thread.compare_exchange_strong(expected, gettid())) {
        if (expected == gettid()) {
            // the teardown itself raised a fault: running it again cannot succeed
            _exit(EXIT_FAILURE);
        }
        if (signum == SIGTERM || signum == SIGINT || signum == SIGQUIT || signum == SIGPIPE) {
            return; // shutdown already in progress on another thread
        }
        // a fault on another thread while shutting down: park it until exit() ends the process
        for (;;) {
            pause();
        }
    }

    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "shutting down server");

    if (signum == SIGSEGV) {
        CALF_PRINT_COLOR(CALF_CLI_LEVEL_ERROR, "Segfault detected!");
    }

#ifdef CAPIO_COVERAGE
    // skip unsafe global teardown in coverage runs until backend shutdown is synchronized.
    __gcov_dump();
    std::_Exit(EXIT_SUCCESS);
#endif

    // free all the memory used
    discovery_service->stop();
    delete client_manager;
    delete storage_manager;
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_WARNING, "data_buffers cleanup completed");

    delete backend;

    delete discovery_service;

    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "shutdown completed");

    exit(EXIT_SUCCESS);
}

void setup_signal_handlers() {
    START_LOG(gettid(), "call()");
    static struct sigaction sigact;
    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_sigaction = sig_term_handler;
    sigact.sa_flags     = SA_SIGINFO;
    int res             = sigaction(SIGTERM, &sigact, nullptr);
    res                 = res | sigaction(SIGILL, &sigact, nullptr);
    res                 = res | sigaction(SIGABRT, &sigact, nullptr);
    res                 = res | sigaction(SIGFPE, &sigact, nullptr);
    res                 = res | sigaction(SIGSEGV, &sigact, nullptr);
    res                 = res | sigaction(SIGQUIT, &sigact, nullptr);
    res                 = res | sigaction(SIGPIPE, &sigact, nullptr);
    res                 = res | sigaction(SIGINT, &sigact, nullptr);
    if (res == -1) {
        ERR_EXIT("sigaction for SIGTERM");
    }

    CALF_PRINT_COLOR(CALF_CLI_LEVEL_STATUS, "Installed signal handlers");
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

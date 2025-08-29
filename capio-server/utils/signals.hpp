#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <include/communication-service/data-plane/backend_interface.hpp>
#include <include/communication-service/capio_communication_service.hpp>

#include <csignal>

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

/**
 * @brief Generic handler for incoming signals
 *
 * @param signum
 * @param info
 * @param ptr
 */
inline void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    if (gettid() != capio_global_configuration->CAPIO_SERVER_MAIN_PID) {
        return;
    }
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);
    server_println();
    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "shutting down server");

    if (signum == SIGSEGV) {
        server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Segfault detected!");
    }

#ifdef CAPIO_COVERAGE
    __gcov_dump();
#endif

    delete request_handlers_engine;
    delete fs_monitor;
    delete capio_communication_service;
    delete shm_canary;

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Bye!");
    exit(EXIT_SUCCESS);
}

inline void sig_usr1_handler(int signum, siginfo_t *info, void *ptr) {
    if (gettid() != capio_global_configuration->CAPIO_SERVER_MAIN_PID) {
        return;
    }
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, "Received request for graceful shutdown!");
    capio_global_configuration->termination_phase = true;
}

/**
 * @brief Set the up signal handlers
 *
 */
inline void setup_signal_handlers() {
    START_LOG(gettid(), "call()");
    static struct sigaction sigact, sigact_usr1;
    memset(&sigact, 0, sizeof(sigact));
    memset(&sigact, 0, sizeof(sigact_usr1));
    sigact.sa_sigaction      = sig_term_handler;
    sigact.sa_flags          = SA_SIGINFO;
    sigact_usr1.sa_sigaction = sig_usr1_handler;
    sigact_usr1.sa_flags     = SA_SIGINFO;
    int res = sigaction(SIGTERM, &sigact, nullptr) | sigaction(SIGILL, &sigact, nullptr) |
              sigaction(SIGABRT, &sigact, nullptr) | sigaction(SIGFPE, &sigact, nullptr) |
              sigaction(SIGSEGV, &sigact, nullptr) | sigaction(SIGQUIT, &sigact, nullptr) |
              sigaction(SIGPIPE, &sigact, nullptr) | sigaction(SIGINT, &sigact, nullptr) |
              sigaction(SIGUSR1, &sigact_usr1, nullptr);
    if (res == -1) {
        ERR_EXIT("sigaction for SIGTERM");
    }
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

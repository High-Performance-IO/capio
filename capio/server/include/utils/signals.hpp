#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <csignal>

#include "remote/backend.hpp"
#include "server_println.hpp"

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                   "sig_term_handler", "shutting down server");

    if (signum == SIGSEGV) {
        server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_ERROR,
                       "sig_term_handler", "Segfault detected!");
    }

    // free all the memory used

    delete client_manager;
    delete storage_manager;

    server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_WARNING,
                   "sig_term_handler", "data_buffers cleanup completed");

#ifdef CAPIO_COVERAGE
    __gcov_dump();
#endif

    delete backend;
    delete shm_canary;

    server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_INFO,
                   "sig_term_handler", "shutdown completed");
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
    server_println(CapioCLEngine::get().getWorkflowName(), CAPIO_LOG_SERVER_CLI_LEVEL_STATUS,
                   "setup_signal_handlers", "Installed signal handlers");
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

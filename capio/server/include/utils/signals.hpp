#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <csignal>

#include "remote/backend.hpp"
#include "remote/discovery.hpp"
#include "server_println.hpp"

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    server_println("shutting down server", CapioCLEngine::get().getWorkflowName(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, __func__);

    if (signum == SIGSEGV) {
        server_println("Segfault detected!", CapioCLEngine::get().getWorkflowName(),
                       CAPIO_LOG_SERVER_CLI_LEVEL_ERROR, __func__);
    }

    // free all the memory used
    discovery_service->stop();
    delete client_manager;
    delete storage_manager;

    server_println("data_buffers cleanup completed", CapioCLEngine::get().getWorkflowName(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_WARNING, __func__);

    delete backend;
    delete client_manager;
    delete storage_manager;

#ifdef CAPIO_COVERAGE
    __gcov_dump();
#endif

    delete discovery_service;

    server_println("shutdown completed", CapioCLEngine::get().getWorkflowName(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_INFO, __func__);
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
    server_println("Installed signal handlers", CapioCLEngine::get().getWorkflowName(),
                   CAPIO_LOG_SERVER_CLI_LEVEL_STATUS, __func__);
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

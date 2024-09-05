#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <csignal>

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    std::cout << std::endl
              << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "shutting down server" << std::endl;

    if (signum == SIGSEGV) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "Segfault detected!" << std::endl;
    }

#ifdef CAPIO_COVERAGE
    __gcov_dump();
#endif

    delete request_handlers_engine;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "request_handlers_engine cleanup completed"
              << std::endl;
    delete ctl_module;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "ctl_module cleanup completed" << std::endl;
    delete fs_monitor;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "fs_monitor cleanup completed" << std::endl;
    delete shm_canary;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << "shutdown completed" << std::endl;

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
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

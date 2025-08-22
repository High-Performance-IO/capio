#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include "../communication-service/data_plane/BackendInterface.hpp"
#include "communication-service/CapioCommunicationService.hpp"

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
    if (gettid() != CAPIO_SERVER_MAIN_PID) {
        return;
    }
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    std::cout << std::endl
              << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
              << "shutting down server" << std::endl;

    if (signum == SIGSEGV) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << " [ " << node_name << " ] "
                  << "Segfault detected!" << std::endl;
    }

#ifdef CAPIO_COVERAGE
    __gcov_dump();
#endif

    delete request_handlers_engine;
    delete fs_monitor;
    delete capio_communication_service;
    delete shm_canary;
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_INFO << " [ " << node_name << " ] "
              << "Bye!" << std::endl;
    exit(EXIT_SUCCESS);
}


inline void sig_usr1_handler(int signum, siginfo_t *info, void *ptr) {
    if (gettid() != CAPIO_SERVER_MAIN_PID) {
        return;
    }
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << " [ " << node_name << " ] "
            << "Recived request for graceful shutdown!" << std::endl;
    termination_phase = true;
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
    sigact.sa_sigaction = sig_term_handler;
    sigact.sa_flags     = SA_SIGINFO;
    sigact_usr1.sa_sigaction = sig_usr1_handler;
    sigact_usr1.sa_flags     = SA_SIGINFO;
    int res             = sigaction(SIGTERM, &sigact, nullptr);
    res                 = res | sigaction(SIGILL, &sigact, nullptr);
    res                 = res | sigaction(SIGABRT, &sigact, nullptr);
    res                 = res | sigaction(SIGFPE, &sigact, nullptr);
    res                 = res | sigaction(SIGSEGV, &sigact, nullptr);
    res                 = res | sigaction(SIGQUIT, &sigact, nullptr);
    res                 = res | sigaction(SIGPIPE, &sigact, nullptr);
    res                 = res | sigaction(SIGINT, &sigact, nullptr);
    res                 = res | sigaction(SIGUSR1, &sigact_usr1, nullptr);
    if (res == -1) {
        ERR_EXIT("sigaction for SIGTERM");
    }
}


/*
 * Defining here the RequestHandlerEngine::handle_termination_phase()
 * to avoid recursive include wihin the header files
 */
inline void RequestHandlerEngine::handle_termination_phase() const {
    START_LOG(capio_syscall(SYS_gettid), "call()");

    while (client_manager->get_connected_posix_client() > 0) {
    }

    LOG("All client steps have terminated. stopping execution of server");
    sig_term_handler(SIGTERM, nullptr, nullptr);
}


#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

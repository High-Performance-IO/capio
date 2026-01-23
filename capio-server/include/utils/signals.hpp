#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <include/communication-service/capio_communication_service.hpp>
#include <include/communication-service/data-plane/backend_interface.hpp>

#include <csignal>

extern ClientManager *client_manager;
extern CapioAPIServer *api_server;
extern CapioFileManager *file_manager;
extern FileSystemMonitor *fs_monitor;
extern RequestHandlerEngine *request_handlers_engine;
extern CapioStorageService *storage_service;

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

inline void sig_usr1_handler(int signum, siginfo_t *info, void *ptr) {
    // Empty function used to Wake up sleeping threads when the API server has received a
    //  Termination request. This way the termination phase condition is re-evaluated and the server
    //  can shut down properly
}

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
    delete api_server;

    server_println(CAPIO_LOG_SERVER_CLI_LEVEL_INFO, "Bye!");
    exit(EXIT_SUCCESS);
}

/**
 * @brief Set the up signal handlers. Note: sigusr1 is only used to wake up from sleep threads
 * waiting on queues
 *
 */
inline void setup_signal_handlers() {
    START_LOG(gettid(), "call()");
    static struct sigaction sigact, sigact_usr1;

    memset(&sigact, 0, sizeof(sigact));
    memset(&sigact_usr1, 0, sizeof(sigact));

    sigact.sa_sigaction = sig_term_handler;
    sigact.sa_flags     = SA_SIGINFO;

    sigact_usr1.sa_sigaction = sig_usr1_handler;
    sigact_usr1.sa_flags     = SA_SIGINFO;

    if ((sigaction(SIGTERM, &sigact, nullptr) | sigaction(SIGILL, &sigact, nullptr) |
         sigaction(SIGABRT, &sigact, nullptr) | sigaction(SIGFPE, &sigact, nullptr) |
         sigaction(SIGSEGV, &sigact, nullptr) | sigaction(SIGQUIT, &sigact, nullptr) |
         sigaction(SIGPIPE, &sigact, nullptr) | sigaction(SIGINT, &sigact, nullptr) |
         sigaction(SIGUSR1, &sigact_usr1, nullptr)) == -1) {
        ERR_EXIT("sigaction for SIGTERM");
    }
}

#endif // CAPIO_SERVER_HANDLERS_SIGNALS_HPP

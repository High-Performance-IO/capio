#ifndef CAPIO_SERVER_HANDLERS_SIGNALS_HPP
#define CAPIO_SERVER_HANDLERS_SIGNALS_HPP

#include <csignal>

#include "remote/backend.hpp"

void sig_term_handler(int signum, siginfo_t *info, void *ptr) {
    START_LOG(gettid(), "call(signal=[%d] (%s) from process with pid=%ld)", signum,
              strsignal(signum), info != nullptr ? info->si_pid : -1);

    std::cout << std::endl
              << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "[ " << node_name << " ] "
              << "shutting down server" << std::endl;

    if (signum == SIGSEGV) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR << "[ " << node_name << " ] "
                  << "Segfault detected!" << std::endl;
    }

    // free all the memory used
    for (auto &it : get_capio_fds()) {
        for (auto &fd : it.second) {
            delete_capio_file_from_tid(it.first, fd);
        }
    }
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "[ " << node_name << " ] "
              << "shm cleanup completed" << std::endl;

    for (auto &p : data_buffers) {
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "[ " << node_name << " ] "
                  << " Deleting data buffer for " << p.second.first->get_name() << std::endl;
        delete p.second.first;
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "[ " << node_name << " ] "
                  << " Deleting data buffer for " << p.second.second->get_name() << std::endl;
        delete p.second.second;
    }

    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_WARNING << "[ " << node_name << " ]"
              << " data_buffers cleanup completed" << std::endl;

    destroy_server();

    delete backend;
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

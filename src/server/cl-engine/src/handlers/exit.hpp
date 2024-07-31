#ifndef CAPIO_EXIT_HPP
#define CAPIO_EXIT_HPP
#include <cl-engine/cl_engine.hpp>

inline void exit_handler(const char *const str) {
    // TODO: register files open for each tid ti register a close
    pid_t tid;
    sscanf(str, "%d", &tid);
    START_LOG(gettid(), "call(tid=%d)", tid);
    CapioFileManager::set_committed(tid);
    client_manager->remove_client(tid);
}

#endif // CAPIO_EXIT_HPP

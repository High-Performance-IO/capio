#ifndef CAPIO_EXIT_HPP
#define CAPIO_EXIT_HPP
#include <cl-engine/cl_engine.hpp>

inline void exit_handler(const char *const str) {
    // TODO: register files open for each tid ti register a close
    int tid;
    char path[PATH_MAX];
    sscanf(str, "%d", &tid);

    // TODO: handle commits on termination
}

#endif // CAPIO_EXIT_HPP

#ifndef CAPIO_EXIT_HPP
#define CAPIO_EXIT_HPP
#include <cl-engine/cl_engine.hpp>
#include <storage-engine/storage_engine.hpp>

inline void exit_handler(const char *const str) {
    //TODO: register files open for each tid ti register a close
    int tid;
    char path[PATH_MAX];
    sscanf(str, "%d", &tid);

    storage_engine->close_all_files(tid);
}

#endif // CAPIO_EXIT_HPP

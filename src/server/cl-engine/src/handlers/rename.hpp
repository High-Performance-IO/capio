#ifndef CAPIO_RENAME_HPP
#define CAPIO_RENAME_HPP
#include <cl-engine/cl_engine.hpp>

inline void rename_handler(const char *const str) {
    int tid;
    char old_path[PATH_MAX], new_path[PATH_MAX];
    sscanf(str, "%d %s %s", &tid, old_path, new_path);

    client_manager->unlock_thread_awaiting_creation(new_path);
    // TODO: gestire le rename?
}

#endif // CAPIO_RENAME_HPP

#ifndef CAPIO_CREATE_HPP
#define CAPIO_CREATE_HPP

#include <cl-engine/cl_engine.hpp>
#include <storage-engine/storage_engine.hpp>

inline void create_handler(const char *const str) {
    int tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);
    std::filesystem::path filename(path);
    storage_engine->create_capio_file(path, tid);
}

#endif // CAPIO_CREATE_HPP

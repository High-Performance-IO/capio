#ifndef CAPIO_CLOSE_HPP
#define CAPIO_CLOSE_HPP
#include <cl-engine/cl_engine.hpp>
#include <storage-engine/storage_engine.hpp>

inline void close_handler(const char *const str) {
    int tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);
    std::filesystem::path filename(path);
    auto n_close = std::get<1>(storage_engine->get_metadata(filename)) + 1;
    storage_engine->update_n_close(filename, n_close);
}

#endif // CAPIO_CLOSE_HPP

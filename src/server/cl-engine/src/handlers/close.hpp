#ifndef CAPIO_CLOSE_HPP
#define CAPIO_CLOSE_HPP
#include <cl-engine/cl_engine.hpp>

inline void close_handler(const char *const str) {
    int tid;
    char path[PATH_MAX];
    sscanf(str, "%d %s", &tid, path);

    START_LOG(gettid(), "call(tid=%d, path=%s)", tid, path);

    std::filesystem::path filename(path);

    if (path == get_capio_dir() || !capio_configuration->file_to_be_handled(filename)) {
        return;
    }

    CapioFileManager::set_committed(path);
}

#endif // CAPIO_CLOSE_HPP

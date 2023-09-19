#ifndef CAPIO_COMMON_UTILS_APP_HPP
#define CAPIO_COMMON_UTILS_APP_HPP

#include <climits>
#include <cstdlib>
#include <filesystem>

#include "filesystem.hpp"
#include "logger.hpp"
#include "syscall.hpp"

const std::string* get_capio_dir() {
    static std::string* capio_dir = nullptr;
    START_LOG(capio_syscall(SYS_gettid), "call()");
    //TODO: if CAPIO_DIR is not set, it should be left as null

    if (capio_dir == nullptr) {

        const char* val = std::getenv("CAPIO_DIR");
        auto buf = std::unique_ptr<char[]>(new char[PATH_MAX]);
        if (val == nullptr) {
            int res = capio_syscall(SYS_getcwd, buf.get(), PATH_MAX);
            if(res == -1) {
                ERR_EXIT("error CAPIO_DIR: current directory not valid");
            }
        }else {
            const char* realpath_res = capio_realpath(val, buf.get());
            if (realpath_res == nullptr) {
                ERR_EXIT("error CAPIO_DIR: directory %s does not exist. [buf=%s]", val, buf.get());
            }
        }
        capio_dir = new std::string(buf.get());
        if (!is_directory(capio_dir->c_str())) {
            ERR_EXIT("dir %s is not a directory", capio_dir->c_str());
        }
    }
    LOG("CAPIO=DIR=%s", capio_dir->c_str());

    return capio_dir;
}

#endif // CAPIO_COMMON_UTILS_APP_HPP

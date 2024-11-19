#include <cerrno>

#ifndef CAPIO_FUNCTIONS_H
#define CAPIO_FUNCTIONS_H

inline int posix_return_value(long res, long *result) {
    START_LOG(capio_syscall(SYS_gettid), "cal(res=%ld)", res);
    if (res != CAPIO_POSIX_SYSCALL_REQUEST_SKIP) {
        *result = (res < 0 ? -errno : res);
        LOG("SYSCALL handled by capio. errno is: %s", res < 0 ? strerror(-errno) : "none");
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    }
    LOG("SYSCALL delegated to the kernel");
    return CAPIO_POSIX_SYSCALL_SKIP;
}

inline bool is_file_to_store_in_memory(std::filesystem::path &path, const long pid) {
    if (paths_to_store_in_memory == nullptr) {
        file_in_memory_request(pid);
    }

    return std::any_of(
        paths_to_store_in_memory->begin(), paths_to_store_in_memory->end(),
        [&path](const std::regex &regex) { return std::regex_match(path.string(), regex); });
}

#endif // CAPIO_FUNCTIONS_H

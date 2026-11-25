#ifndef CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP
#define CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP

#if defined(SYS_close_range)

#include <unordered_map>
#include <vector>

#include "utils/requests.hpp"

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

int close_range_handler(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5,
                        long *result) {
    MEMORY_MODE({ return CAPIO_POSIX_SYSCALL_SKIP; })
    FS_MODE({
        const auto first = static_cast<unsigned>(arg0);
        const auto last  = static_cast<unsigned>(arg1);
        const auto flags = static_cast<unsigned>(arg2);
        const long tid   = syscall_no_intercept(SYS_gettid);
        START_LOG(tid, "call(first=%u, last=%u, flags=%u)", first, last, flags);

        std::vector<int> affected;
        for (const int fd : fd_table->fds()) {
            if (static_cast<unsigned>(fd) >= first && static_cast<unsigned>(fd) <= last) {
                affected.push_back(fd);
            }
        }
        if (affected.empty()) {
            return CAPIO_POSIX_SYSCALL_SKIP;
        }
        // The kernel drops the errors of these closes: flushed first to get them
        const bool only_cloexec = (flags & CLOSE_RANGE_CLOEXEC) != 0;
        std::unordered_map<int, long> errors;
        if (!only_cloexec) {
            for (const int fd : affected) {
                errors.emplace(fd, flush_capio_fd(fd));
            }
        }
        *result = fd_table->closeRange(first, last, flags, [&errors, tid](const int fd) {
            const auto error = errors.find(fd);
            close_request(fd, tid, error == errors.end() ? 0 : error->second);
        });
        return CAPIO_POSIX_SYSCALL_SUCCESS;
    })
}

#endif // SYS_close_range
#endif // CAPIO_POSIX_HANDLERS_CLOSE_RANGE_HPP

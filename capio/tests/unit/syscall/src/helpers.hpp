#ifndef CAPIO_TESTS_UNIT_SYSCALL_HELPERS_HPP
#define CAPIO_TESTS_UNIT_SYSCALL_HELPERS_HPP

#include <gtest/gtest.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/// @brief Whether the tests run with libcapio_posix.so, the variant that keeps files in memory
inline bool memory_variant() {
    const char *preload = std::getenv("LD_PRELOAD");
    return preload != nullptr && std::strstr(preload, "libcapio_posix_fs") == nullptr;
}

inline void write_file(const char *path, const std::string &data) {
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1) << path << ": " << std::strerror(errno);
    EXPECT_EQ(write(fd, data.data(), data.size()), static_cast<ssize_t>(data.size()));
    EXPECT_EQ(close(fd), 0);
}

/// @brief The content of @p path, which must exist: a CAPIO open of a missing file waits for it
inline std::string read_file(const char *path) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return "<" + std::string(std::strerror(errno)) + ">";
    }
    char buffer[64]{};
    const auto count = read(fd, buffer, sizeof(buffer));
    close(fd);
    return {buffer, static_cast<size_t>(count > 0 ? count : 0)};
}

/**
 * @brief Run @p body in a child process and return its exit status, or minus the signal that
 * killed it, and its pid in @p child. The raw syscall, as in exec.cpp: fork() holds the malloc lock
 * while the clone hook allocates in the child.
 */
inline int run_in_child(const std::function<int()> &body, pid_t *child = nullptr) {
    constexpr int TIMEOUT_MS = 10000;
    constexpr int STEP_MS    = 10;
#ifdef SYS_fork
    const auto pid = static_cast<pid_t>(syscall(SYS_fork));
#else
    const auto pid = static_cast<pid_t>(syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0));
#endif
    if (pid == 0) {
        _exit(body());
    }
    if (child != nullptr) {
        *child = pid;
    }
    int status = 0;
    pid_t done = 0;
    for (int waited = 0; waited < TIMEOUT_MS && done == 0; waited += STEP_MS) {
        done = waitpid(pid, &status, WNOHANG);
        if (done == 0) {
            usleep(STEP_MS * 1000);
        }
    }
    if (done == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        ADD_FAILURE() << "the child did not terminate";
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
}

#endif // CAPIO_TESTS_UNIT_SYSCALL_HELPERS_HPP

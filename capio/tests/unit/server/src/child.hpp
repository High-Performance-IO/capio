#ifndef CAPIO_SERVER_TESTS_CHILD_HPP
#define CAPIO_SERVER_TESTS_CHILD_HPP

#include <gtest/gtest.h>

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

/**
 * @brief Runs @p body in a forked child, for what a process can do only once (MPI_Init) or keeps
 * per process (the MTCL manager).
 * @return The exit status of the child: 0 when none of its checks failed
 */
template <typename Body> int run_in_child(Body body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        body();
        const int status = testing::Test::HasFailure() ? 1 : 0;
        std::fflush(nullptr);
#ifdef CAPIO_COVERAGE
        __gcov_dump(); // _exit does not write it
#endif
        _exit(status);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

#endif // CAPIO_SERVER_TESTS_CHILD_HPP

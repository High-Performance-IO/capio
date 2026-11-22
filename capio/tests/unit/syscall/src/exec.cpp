#include <gtest/gtest.h>

#include <csignal>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * execve keeps the tid, so the program registers again with the server under the same one. A
 * second exec used to leave the client and the server on different response buffers, and the
 * client waited forever.
 */
TEST(SystemCallTest, TestChainedExec) {
    constexpr int TIMEOUT_MS = 10000;
    constexpr int STEP_MS    = 10;

    char name[] = "bash", norc[] = "--norc", opt[] = "-c";
    char command[] = "exec bash --norc -c 'exec bash --norc -c \"exec bash --norc -c :\"'";
    char *args[]   = {name, norc, opt, command, nullptr};

    // The raw syscall, as in main.cpp. fork() holds the malloc lock and the clone hook allocates
    // in the child, and after posix_spawn the child's hook has replaced this thread's queues.
#ifdef SYS_fork
    const auto pid = static_cast<pid_t>(syscall(SYS_fork));
#else
    const auto pid = static_cast<pid_t>(syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0));
#endif
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        execvp(name, args);
        _exit(127);
    }

    int status = 0;
    pid_t done = 0;
    for (int waited = 0; waited < TIMEOUT_MS && done == 0; waited += STEP_MS) {
        done = waitpid(pid, &status, WNOHANG);
        usleep(STEP_MS * 1000);
    }
    if (done == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        FAIL() << "chained exec did not terminate";
    }
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

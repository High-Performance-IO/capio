#include <gtest/gtest.h>

#include <csignal>
#include <filesystem>
#include <string>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "helpers.hpp"

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

TEST(SystemCallTest, TestCloseOnExecDescriptorsAreClosedByExec) {
    if (memory_variant()) {
        GTEST_SKIP() << "#314: the memory variant leaves them open on /dev/null";
    }
    const int status = run_in_child([] {
        const int fd = open("exec_cloexec", O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) {
            return 90;
        }
        // Exits 0 only if fd is not the file in the new program. The number itself may be open
        // again: the new program's libcapio can reuse it, e.g. for its log
        const std::string command =
            "[ /proc/self/fd/" + std::to_string(fd) + " -ef exec_cloexec ] && exit 1; exit 0";
        execlp("bash", "bash", "--norc", "-c", command.c_str(), nullptr);
        return 91;
    });
    EXPECT_EQ(status, 0);
    EXPECT_EQ(unlink("exec_cloexec"), 0);
}

TEST(SystemCallTest, TestExecFromAnotherThreadKeepsTheDescriptors) {
    if (memory_variant()) {
        GTEST_SKIP() << "#314: the snapshot is named after the thread, the new program reads it "
                        "under the process id";
    }
    write_file("exec_thread", "AB");
    const int status = run_in_child([] {
        const int fd = open("exec_thread", O_RDONLY);
        if (fd < 0) {
            return 90;
        }
        std::thread([fd] {
            const std::string command =
                "read -r line <&" + std::to_string(fd) + "; [ \"$line\" = AB ]";
            execlp("bash", "bash", "--norc", "-c", command.c_str(), nullptr);
            _exit(91);
        }).join();
        return 92;
    });
    EXPECT_EQ(status, 0);
    EXPECT_EQ(unlink("exec_thread"), 0);
}

// The server removes the queues of a client that exits (#319)
TEST(SystemCallTest, TestAnExitedClientLeavesNoSharedMemory) {
    constexpr int TIMEOUT_MS = 5000;
    constexpr int STEP_MS    = 10;

    pid_t child      = 0;
    const int status = run_in_child(
        [] {
            write_file("exec_shm", "AB");
            return read_file("exec_shm") == "AB" ? 0 : 1;
        },
        &child);
    EXPECT_EQ(status, 0);

    // The queues of a thread end with its id. The server removes them once it reads the exit.
    const std::string suffix = "_" + std::to_string(child);
    std::vector<std::string> left;
    for (int waited = 0; waited < TIMEOUT_MS; waited += STEP_MS) {
        left.clear();
        for (const auto &entry : std::filesystem::directory_iterator("/dev/shm")) {
            const std::string name = entry.path().filename();
            if (name.size() > suffix.size() &&
                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                left.push_back(name);
            }
        }
        if (left.empty()) {
            break;
        }
        usleep(STEP_MS * 1000);
    }
    EXPECT_TRUE(left.empty()) << left.front();
    EXPECT_EQ(unlink("exec_shm"), 0);
}

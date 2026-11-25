#ifndef CAPIO_TEST_UNIT_POSIX_FILE_DESCRIPTOR_TABLE_HPP
#define CAPIO_TEST_UNIT_POSIX_FILE_DESCRIPTOR_TABLE_HPP

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <linux/close_range.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

#include "common/constants.hpp"
#include "utils/file_descriptor_table.hpp"

namespace {

/**
 * While one is alive, the descriptors that the table closes are recorded instead of closed, and the
 * system calls in results return what it says instead of reaching the kernel: see
 * syscall_no_intercept below. The other system calls, and all of them when none is alive, reach the
 * kernel, as the other tests of the client need.
 */
class MockSyscalls final {
    std::mutex _mutex;
    std::condition_variable _released;
    bool _blocking = false;
    bool _closing  = false;

  public:
    static inline std::atomic<MockSyscalls *> active{nullptr};

    std::vector<int> closed;
    long result = 0;                        ///< what close returns
    std::unordered_map<long, long> results; ///< system call -> what it returns: 0, or -errno

    MockSyscalls() { active = this; }
    ~MockSyscalls() { active = nullptr; }
    MockSyscalls(const MockSyscalls &)            = delete;
    MockSyscalls &operator=(const MockSyscalls &) = delete;

    long close(const int fd) {
        std::unique_lock lock(_mutex);
        closed.push_back(fd);
        _closing = true;
        _released.notify_all();
        _released.wait(lock, [this] { return !_blocking; });
        return result;
    }

    /// @brief Make the next closes wait, inside the table, until release()
    void block() {
        const std::lock_guard lock(_mutex);
        _blocking = true;
        _closing  = false;
    }

    void waitUntilClosing() {
        std::unique_lock lock(_mutex);
        _released.wait(lock, [this] { return _closing; });
    }

    void release() {
        const std::lock_guard lock(_mutex);
        _blocking = false;
        _released.notify_all();
    }
};

/// @brief A thread id of no thread, so that no other process has its snapshot
long unusedTid() {
    static long next = 0;
    return 1'000'000'000L + getpid() * 1000L + next++;
}

std::filesystem::path snapshotFile(const long tid) {
    return std::string(SHM_SNAPSHOT_PREFIX) + std::to_string(tid);
}

/// @brief Write @p bytes as the snapshot of @p tid, as an exec of another program could leave it
void writeSnapshot(const long tid, const std::string &bytes) {
    std::ofstream(snapshotFile(tid), std::ios::binary) << bytes;
}

#ifdef CAPIO_COVERAGE
extern "C" void __gcov_dump(void);
#endif

/**
 * @brief fork. With coverage, a direct call of fork is replaced by one that resets the counters of
 * the child after the fork handlers have run, so that their lines would look never run: calling it
 * through a pointer keeps them.
 */
pid_t forkKeepingCoverage() {
#ifdef CAPIO_COVERAGE
    pid_t (*const volatile call)() = fork;
    return call();
#else
    return fork();
#endif
}

/// @brief The end of a forked child, which keeps what it has covered
[[noreturn]] void exitChild(const int status) {
#ifdef CAPIO_COVERAGE
    __gcov_dump(); // _exit does not write it
#endif
    _exit(status);
}

} // namespace

namespace {

/*
 * A system call straight to the kernel, as syscall_no_intercept makes it: libsyscall_intercept
 * calls syscall_no_intercept itself for every system call of libc it intercepts, so going through
 * libc again (syscall()) would come back here forever.
 * @return the result of the call, or -errno
 */
long kernelSyscall(const long number, const long a0, const long a1, const long a2, const long a3,
                   const long a4, const long a5) {
#if defined(__x86_64__)
    long result;
    register long r10 asm("r10") = a3;
    register long r8 asm("r8")   = a4;
    register long r9 asm("r9")   = a5;
    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(number), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return result;
#elif defined(__aarch64__)
    register long x8 asm("x8") = number;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    asm volatile("svc 0"
                 : "+r"(x0)
                 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                 : "memory");
    return x0;
#else
#error "kernelSyscall: unsupported architecture"
#endif
}

} // namespace

/**
 * The test executable defines it, so its calls come here instead of to libsyscall_intercept: those
 * of the classes of the client, and those of libsyscall_intercept itself.
 */
extern "C" long syscall_no_intercept(const long syscall_number, ...) {
    va_list arguments;
    va_start(arguments, syscall_number);
    long arg[6];
    for (long &argument : arg) {
        argument = va_arg(arguments, long);
    }
    va_end(arguments);

    if (MockSyscalls *mock = MockSyscalls::active; mock != nullptr) {
        if (const auto found = mock->results.find(syscall_number); found != mock->results.end()) {
            return found->second;
        }
        if (syscall_number == SYS_close) {
            return mock->close(static_cast<int>(arg[0]));
        }
    }
    return kernelSyscall(syscall_number, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
}

TEST(FileDescriptorTableTest, StartsEmpty) {
    const MockSyscalls system_calls;
    const FileDescriptorTable table;
    EXPECT_TRUE(table.fds().empty());
    EXPECT_FALSE(table.contains(3));
    EXPECT_FALSE(table.containsPath("/capio/f"));
}

TEST(FileDescriptorTableTest, AddKnowsTheDescriptorAndItsPath) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 10, 100, O_WRONLY, true);

    EXPECT_TRUE(table.contains(3));
    EXPECT_TRUE(table.containsPath("/capio/f"));
    EXPECT_EQ(table.path(3), "/capio/f");
    EXPECT_EQ(table.offset(3), 10);
    EXPECT_EQ(table.size(3), 100);
    EXPECT_EQ(table.flags(3), O_WRONLY);
    EXPECT_TRUE(table.cloexec(3));
    EXPECT_EQ(table.fds(), std::vector<int>{3});
}

TEST(FileDescriptorTableTest, SettersChangeOneDescriptor) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.add(4, "/capio/f", 0, 0, O_RDONLY, false);

    table.setOffset(3, 42);
    table.setFlags(3, O_RDWR);
    table.setCloexec(3, true);

    EXPECT_EQ(table.offset(3), 42);
    EXPECT_EQ(table.flags(3), O_RDWR);
    EXPECT_TRUE(table.cloexec(3));
    EXPECT_EQ(table.offset(4), 0); // opened on its own: it does not share the offset
    EXPECT_EQ(table.flags(4), O_RDONLY);
    EXPECT_FALSE(table.cloexec(4));
    EXPECT_FALSE(table.sharesOffset(3, 4));
}

TEST(FileDescriptorTableTest, UnknownDescriptorHasNoPath) {
    const MockSyscalls system_calls;
    const FileDescriptorTable table;
    EXPECT_EQ(table.findPath(3), std::nullopt);
    EXPECT_THROW((void) table.path(3), std::out_of_range);
    EXPECT_THROW((void) table.offset(3), std::out_of_range);
}

TEST(FileDescriptorTableTest, FindPathOfAKnownDescriptor) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    EXPECT_EQ(table.findPath(3), "/capio/f");
}

TEST(FileDescriptorTableTest, PathWithoutDescriptors) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.addPath("/capio/d");
    EXPECT_TRUE(table.containsPath("/capio/d"));
    EXPECT_TRUE(table.fds().empty());
}

TEST(FileDescriptorTableTest, CopySharesTheOffsetButNotCloseOnExec) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 5, 100, O_WRONLY, false);
    table.dup(3, 4, true);

    EXPECT_EQ(table.path(4), "/capio/f");
    EXPECT_EQ(table.offset(4), 5);
    EXPECT_EQ(table.size(4), 100);
    EXPECT_EQ(table.flags(4), O_WRONLY);
    EXPECT_TRUE(table.cloexec(4));
    EXPECT_FALSE(table.cloexec(3));
    EXPECT_TRUE(table.sharesOffset(3, 4));

    table.setOffset(4, 50);
    EXPECT_EQ(table.offset(3), 50);
}

TEST(FileDescriptorTableTest, RemovingOneCopyKeepsTheOther) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);
    table.remove(3);

    EXPECT_FALSE(table.contains(3));
    EXPECT_TRUE(table.contains(4));
    EXPECT_TRUE(table.containsPath("/capio/f"));
    EXPECT_EQ(table.path(4), "/capio/f");
}

TEST(FileDescriptorTableTest, RemovingAnUnknownDescriptorDoesNothing) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.remove(4);
    EXPECT_EQ(table.fds(), std::vector<int>{3});
}

// The number of a descriptor that the kernel closed without CAPIO knowing is given again
TEST(FileDescriptorTableTest, AddingOntoATakenNumberReplacesIt) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/old", 0, 0, O_RDONLY, false);
    table.add(3, "/capio/new", 7, 0, O_WRONLY, false);

    EXPECT_EQ(table.path(3), "/capio/new");
    EXPECT_EQ(table.offset(3), 7);
    table.removePath("/capio/old"); // the old path does not list the number any more
    EXPECT_TRUE(table.contains(3));
    EXPECT_EQ(table.path(3), "/capio/new");
}

TEST(FileDescriptorTableTest, CopyOntoATakenNumberReplacesIt) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.add(4, "/capio/g", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);

    EXPECT_EQ(table.path(4), "/capio/f");
    EXPECT_TRUE(table.sharesOffset(3, 4));
    table.removePath("/capio/g");
    EXPECT_TRUE(table.contains(4));
}

TEST(FileDescriptorTableTest, RemovingAPathRemovesOnlyItsDescriptors) {
    MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);
    table.add(5, "/capio/g", 0, 0, O_RDONLY, false);
    table.removePath("/capio/f");

    EXPECT_FALSE(table.containsPath("/capio/f"));
    EXPECT_FALSE(table.contains(3));
    EXPECT_FALSE(table.contains(4));
    EXPECT_EQ(table.fds(), std::vector<int>{5});
    EXPECT_TRUE(system_calls.closed.empty()); // forgotten, not closed
}

TEST(FileDescriptorTableTest, RemovingAnUnknownPathDoesNothing) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.removePath("/capio/g");
    EXPECT_TRUE(table.contains(3));
}

TEST(FileDescriptorTableTest, RenamingAPathMovesItsDescriptors) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/old", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);
    table.renamePath("/capio/old", "/capio/new");

    EXPECT_FALSE(table.containsPath("/capio/old"));
    EXPECT_TRUE(table.containsPath("/capio/new"));
    EXPECT_EQ(table.path(3), "/capio/new");
    EXPECT_EQ(table.path(4), "/capio/new");
    table.removePath("/capio/new");
    EXPECT_TRUE(table.fds().empty());
}

TEST(FileDescriptorTableTest, RenamingAnUnknownPathDoesNothing) {
    const MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.renamePath("/capio/g", "/capio/h");

    EXPECT_FALSE(table.containsPath("/capio/h"));
    EXPECT_EQ(table.path(3), "/capio/f");
}

TEST(FileDescriptorTableTest, CloseClosesInTheKernelAndForgets) {
    MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.dup(3, 4, false);

    EXPECT_EQ(table.close(3), 0);
    EXPECT_EQ(system_calls.closed, std::vector<int>{3});
    EXPECT_FALSE(table.contains(3));
    EXPECT_TRUE(table.contains(4));
}

// Linux frees the number also when close fails, e.g. with an I/O error on NFS
TEST(FileDescriptorTableTest, CloseAnnouncesWhatTheKernelReturned) {
    MockSyscalls system_calls;
    system_calls.result = -EIO;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);

    std::vector<long> announced;
    EXPECT_EQ(table.close(3, [&](const long result) { announced.push_back(result); }), -EIO);
    EXPECT_EQ(announced, std::vector<long>{-EIO});
}

// The server knows the descriptor by its number: the close of what had it reaches the server
// before another thread that gets the number can tell the server
TEST(FileDescriptorTableTest, CloseAnnouncesBeforeTheNumberIsAddedAgain) {
    MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);

    std::atomic<bool> announcing{false}, release{false}, added{false};
    std::thread closer([&] {
        table.close(3, [&](long) {
            announcing = true;
            while (!release) {
                std::this_thread::yield();
            }
        });
    });
    while (!announcing) {
        std::this_thread::yield();
    }
    std::thread adder([&] {
        table.add(3, "/capio/g", 0, 0, O_WRONLY, false);
        added = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(added);
    release = true;
    closer.join();
    adder.join();
    EXPECT_EQ(table.path(3), "/capio/g");
}

TEST(FileDescriptorTableTest, CloseRangeForgetsAndAnnouncesTheDescriptorsInIt) {
    MockSyscalls system_calls;
    system_calls.results[SYS_close_range] = 0;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.dup(3, 5, false);
    table.add(7, "/capio/g", 0, 0, O_WRONLY, false);

    std::vector<int> announced;
    EXPECT_EQ(table.closeRange(3, 6, 0, [&](const int fd) { announced.push_back(fd); }), 0);
    std::sort(announced.begin(), announced.end());
    EXPECT_EQ(announced, (std::vector<int>{3, 5}));
    EXPECT_EQ(table.fds(), std::vector<int>{7});
    EXPECT_TRUE(system_calls.closed.empty()); // one close_range, no close
}

TEST(FileDescriptorTableTest, CloseRangeCanOnlyMakeThemCloseOnExec) {
    MockSyscalls system_calls;
    system_calls.results[SYS_close_range] = 0;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.add(7, "/capio/g", 0, 0, O_WRONLY, false);

    std::vector<int> announced;
    EXPECT_EQ(
        table.closeRange(0, 5, CLOSE_RANGE_CLOEXEC, [&](const int fd) { announced.push_back(fd); }),
        0);
    EXPECT_TRUE(announced.empty());
    EXPECT_TRUE(table.cloexec(3));
    EXPECT_FALSE(table.cloexec(7));
    EXPECT_EQ(table.fds().size(), 2U);
}

TEST(FileDescriptorTableTest, FailedCloseRangeChangesNothing) {
    MockSyscalls system_calls;
    system_calls.results[SYS_close_range] = -EINVAL;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);

    bool announced = false;
    EXPECT_EQ(table.closeRange(0, 5, 0, [&](int) { announced = true; }), -EINVAL);
    EXPECT_FALSE(announced);
    EXPECT_TRUE(table.contains(3));
}

TEST(FileDescriptorTableTest, FailedCloseForgetsTooAndReturnsTheError) {
    MockSyscalls system_calls;
    system_calls.result = -EIO;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);

    EXPECT_EQ(table.close(3), -EIO);
    EXPECT_FALSE(table.contains(3));
}

// No other thread sees the number in the table while the kernel closes it
TEST(FileDescriptorTableTest, CloseHoldsTheTableUntilTheKernelHasClosed) {
    MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    system_calls.block();

    std::thread closer([&] { table.close(3); });
    system_calls.waitUntilClosing();
    std::atomic<bool> added{false};
    std::thread opener([&] {
        table.add(3, "/capio/g", 0, 0, O_WRONLY, false);
        added = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(added); // waits for the close
    system_calls.release();
    closer.join();
    opener.join();

    EXPECT_TRUE(added);
    EXPECT_EQ(table.path(3), "/capio/g");
}

TEST(FileDescriptorTableTest, ThreadsKeepTheTableConsistent) {
    MockSyscalls system_calls;
    FileDescriptorTable table;
    constexpr int THREADS = 8, ROUNDS = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&table, t] {
            const int fd = 100 + 2 * t, copy = fd + 1;
            const std::string path = "/capio/thread" + std::to_string(t);
            for (int i = 0; i < ROUNDS; ++i) {
                table.add(fd, path, i, 0, O_WRONLY, false);
                table.dup(fd, copy, false);
                table.setOffset(copy, i + 1);
                if (table.offset(fd) != i + 1 || table.path(copy) != path) {
                    ADD_FAILURE() << "thread " << t << " round " << i;
                    return;
                }
                table.remove(fd);
                table.close(copy);
            }
            table.add(fd, path, 0, 0, O_WRONLY, false);
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    auto fds = table.fds();
    std::sort(fds.begin(), fds.end());
    std::vector<int> expected;
    for (int t = 0; t < THREADS; ++t) {
        expected.push_back(100 + 2 * t);
    }
    EXPECT_EQ(fds, expected);
    EXPECT_EQ(system_calls.closed.size(), static_cast<size_t>(THREADS * ROUNDS));
}

// A thread holds the table when another one forks: the child must still be able to use it
TEST(FileDescriptorTableTest, ForkedChildGetsAFreeTable) {
    FileDescriptorTable::protectAcrossFork();
    MockSyscalls system_calls;
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    system_calls.block();

    std::thread closer([&] { table.close(3); });
    system_calls.waitUntilClosing();
    std::thread releaser([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        system_calls.release();
    });
    const pid_t child = forkKeepingCoverage(); // waits for the close, which holds the table
    if (child == 0) {
        table.add(4, "/capio/g", 0, 0, O_WRONLY, false);
        exitChild(table.contains(4) && !table.contains(3) ? 0 : 1);
    }
    closer.join();
    releaser.join();
    ASSERT_GT(child, 0);

    int status = 0;
    for (int i = 0; i < 500 && waitpid(child, &status, WNOHANG) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (waitpid(child, &status, WNOHANG) == 0) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        FAIL() << "the child waits for the table";
    }
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST(FileDescriptorTableTest, SnapshotRestoresTheDescriptorsKeptByTheExec) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 10, 100, O_WRONLY, false);
    before.add(4, "/capio/g", 20, 200, O_RDONLY, true); // closed by the exec
    before.addPath("/capio/d");
    before.createSnapshot(tid);

    const FileDescriptorTable after(tid);
    EXPECT_EQ(after.fds(), std::vector<int>{3});
    EXPECT_EQ(after.path(3), "/capio/f");
    EXPECT_EQ(after.offset(3), 10);
    EXPECT_EQ(after.size(3), 100);
    EXPECT_EQ(after.flags(3), O_WRONLY);
    EXPECT_FALSE(after.cloexec(3));
    EXPECT_FALSE(after.containsPath("/capio/g"));
}

// The FS variant tells the server of them once the exec has worked
TEST(FileDescriptorTableTest, SnapshotListsTheDescriptorsClosedByTheExec) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    before.add(4, "/capio/g", 0, 0, O_RDONLY, true);
    before.dup(4, 6, true);
    before.createSnapshot(tid);

    const FileDescriptorTable after(tid);
    auto closed = after.closedByExec();
    std::sort(closed.begin(), closed.end());
    EXPECT_EQ(closed, (std::vector<int>{4, 6}));
    EXPECT_EQ(after.fds(), std::vector<int>{3});
    EXPECT_TRUE(FileDescriptorTable().closedByExec().empty());
    EXPECT_TRUE(FileDescriptorTable(unusedTid()).closedByExec().empty());
}

TEST(FileDescriptorTableTest, SnapshotKeepsCopiesSharingTheirOffset) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 10, 0, O_WRONLY, true);
    before.dup(3, 4, false);
    before.dup(3, 5, false);
    before.add(6, "/capio/f", 0, 0, O_WRONLY, false); // opened again: an offset of its own
    before.createSnapshot(tid);

    FileDescriptorTable after(tid);
    auto fds = after.fds();
    std::sort(fds.begin(), fds.end());
    EXPECT_EQ(fds, (std::vector<int>{4, 5, 6}));
    EXPECT_TRUE(after.sharesOffset(4, 5));
    EXPECT_FALSE(after.sharesOffset(4, 6));
    after.setOffset(4, 30);
    EXPECT_EQ(after.offset(5), 30);
    EXPECT_EQ(after.offset(6), 0);
}

TEST(FileDescriptorTableTest, SnapshotIsReadOnce) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    before.createSnapshot(tid);

    const FileDescriptorTable first(tid);
    const FileDescriptorTable second(tid);
    EXPECT_TRUE(first.contains(3));
    EXPECT_TRUE(second.fds().empty());
}

TEST(FileDescriptorTableTest, NoSnapshotMeansAnEmptyTable) {
    const FileDescriptorTable table(unusedTid());
    EXPECT_TRUE(table.fds().empty());
}

// An exec that fails leaves its snapshot: the next exec must not restore it
TEST(FileDescriptorTableTest, SnapshotReplacesTheOneOfAFailedExec) {
    const long tid = unusedTid();
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.createSnapshot(tid); // the exec fails
    table.remove(3);
    table.add(4, "/capio/g", 0, 0, O_WRONLY, false);
    table.createSnapshot(tid);

    const FileDescriptorTable after(tid);
    EXPECT_EQ(after.fds(), std::vector<int>{4});
}

TEST(FileDescriptorTableTest, EmptyTableRemovesTheSnapshotOfAFailedExec) {
    const long tid = unusedTid();
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.createSnapshot(tid); // the exec fails
    table.remove(3);
    table.createSnapshot(tid);

    const FileDescriptorTable after(tid);
    EXPECT_TRUE(after.fds().empty());
}

// The snapshot is a file of /dev/shm, which the program after the exec removes once read
TEST(FileDescriptorTableTest, SnapshotIsAFileOfDevShm) {
    const long tid                   = unusedTid();
    const std::filesystem::path file = snapshotFile(tid);
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    before.createSnapshot(tid);
    EXPECT_EQ(file.parent_path(), "/dev/shm");
    EXPECT_TRUE(std::filesystem::exists(file));

    const FileDescriptorTable after(tid);
    EXPECT_FALSE(std::filesystem::exists(file));
}

// A snapshot that cannot be made: the next program starts with no CAPIO file
TEST(FileDescriptorTableTest, SnapshotThatCannotBeCreated) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    {
        MockSyscalls system_calls;
        system_calls.results[SYS_openat] = -ENOSPC;
        before.createSnapshot(tid);
    }
    EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid)));
    EXPECT_TRUE(FileDescriptorTable(tid).fds().empty());
}

// A snapshot that cannot be made as long as the table: none is left half made
TEST(FileDescriptorTableTest, SnapshotThatCannotBeSized) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    {
        MockSyscalls system_calls;
        system_calls.results[SYS_ftruncate] = -ENOSPC;
        before.createSnapshot(tid);
    }
    EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid)));
    EXPECT_TRUE(FileDescriptorTable(tid).fds().empty());
}

TEST(FileDescriptorTableTest, SnapshotThatCannotBeMapped) {
    const long tid = unusedTid();
    FileDescriptorTable before;
    before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    {
        MockSyscalls system_calls;
        system_calls.results[SYS_mmap] = -ENOMEM;
        before.createSnapshot(tid);
    }
    EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid)));
    EXPECT_TRUE(FileDescriptorTable(tid).fds().empty());
}

// A snapshot that cannot be read: the program starts with no CAPIO file, and the snapshot is gone
TEST(FileDescriptorTableTest, SnapshotThatCannotBeRead) {
    for (const long system_call : {SYS_fstat, SYS_mmap}) {
        const long tid = unusedTid();
        FileDescriptorTable before;
        before.add(3, "/capio/f", 0, 0, O_WRONLY, false);
        before.createSnapshot(tid);

        std::vector<int> fds;
        {
            MockSyscalls system_calls;
            system_calls.results[system_call] = -EIO;
            fds                               = FileDescriptorTable(tid).fds();
        }
        EXPECT_TRUE(fds.empty()) << system_call;
        EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid))) << system_call;
    }
}

TEST(FileDescriptorTableTest, SnapshotShorterThanItsHeader) {
    const long tid = unusedTid();
    writeSnapshot(tid, "x");

    EXPECT_TRUE(FileDescriptorTable(tid).fds().empty());
    EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid)));
}

// A header that counts more entries than the file holds: none is read past its end
TEST(FileDescriptorTableTest, SnapshotShorterThanItsEntries) {
    const long tid     = unusedTid();
    const size_t count = 1000;
    writeSnapshot(tid, std::string(reinterpret_cast<const char *>(&count), sizeof(count)));

    EXPECT_TRUE(FileDescriptorTable(tid).fds().empty());
    EXPECT_FALSE(std::filesystem::exists(snapshotFile(tid)));
}

#endif // CAPIO_TEST_UNIT_POSIX_FILE_DESCRIPTOR_TABLE_HPP

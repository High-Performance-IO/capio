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
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>

#include <libsyscall_intercept_hook_point.h>
#include <sys/syscall.h>

#include "utils/file_descriptor_table.hpp"

namespace {

/**
 * While one is alive, the descriptors that the table closes are recorded instead of closed: see
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
    long result = 0; ///< what close returns

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

} // namespace

/// The test executable defines it, so its calls, those of the client classes included, come here
/// instead of to libsyscall_intercept
extern "C" long syscall_no_intercept(const long syscall_number, ...) {
    va_list arguments;
    va_start(arguments, syscall_number);
    long arg[6];
    for (long &argument : arg) {
        argument = va_arg(arguments, long);
    }
    va_end(arguments);

    if (MockSyscalls *mock = MockSyscalls::active; mock != nullptr && syscall_number == SYS_close) {
        return mock->close(static_cast<int>(arg[0]));
    }
    const long result = syscall(syscall_number, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
    return result == -1 ? -errno : result; // as syscall_no_intercept returns errors
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
    const pid_t child = fork(); // waits for the close, which holds the table
    if (child == 0) {
        table.add(4, "/capio/g", 0, 0, O_WRONLY, false);
        _exit(table.contains(4) && !table.contains(3) ? 0 : 1);
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
    const MockSyscalls system_calls;
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

TEST(FileDescriptorTableTest, SnapshotKeepsCopiesSharingTheirOffset) {
    const MockSyscalls system_calls;
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
    const MockSyscalls system_calls;
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
    const MockSyscalls system_calls;
    const FileDescriptorTable table(unusedTid());
    EXPECT_TRUE(table.fds().empty());
}

// An exec that fails leaves its snapshot: the next exec must not restore it
TEST(FileDescriptorTableTest, SnapshotReplacesTheOneOfAFailedExec) {
    const MockSyscalls system_calls;
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
    const MockSyscalls system_calls;
    const long tid = unusedTid();
    FileDescriptorTable table;
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.createSnapshot(tid); // the exec fails
    table.remove(3);
    table.createSnapshot(tid);

    const FileDescriptorTable after(tid);
    EXPECT_TRUE(after.fds().empty());
}

#endif // CAPIO_TEST_UNIT_POSIX_FILE_DESCRIPTOR_TABLE_HPP

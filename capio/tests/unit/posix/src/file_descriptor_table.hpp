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

#include "utils/file_descriptor_table.hpp"

namespace {

// The system calls of the table: records the descriptors it closes, instead of closing them
class RecordingSystemCalls final {
    std::mutex _mutex;
    std::condition_variable _released;
    bool _blocking = false;
    bool _closing  = false;

  public:
    std::vector<int> closed;
    long result = 0;

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

using Table = BasicFileDescriptorTable<RecordingSystemCalls>;

} // namespace

TEST(FileDescriptorTableTest, StartsEmpty) {
    RecordingSystemCalls system_calls;
    const Table table(system_calls);
    EXPECT_TRUE(table.fds().empty());
    EXPECT_FALSE(table.contains(3));
    EXPECT_FALSE(table.containsPath("/capio/f"));
}

TEST(FileDescriptorTableTest, AddKnowsTheDescriptorAndItsPath) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    const Table table(system_calls);
    EXPECT_EQ(table.findPath(3), std::nullopt);
    EXPECT_THROW((void) table.path(3), std::out_of_range);
    EXPECT_THROW((void) table.offset(3), std::out_of_range);
}

TEST(FileDescriptorTableTest, FindPathOfAKnownDescriptor) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    EXPECT_EQ(table.findPath(3), "/capio/f");
}

TEST(FileDescriptorTableTest, PathWithoutDescriptors) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.addPath("/capio/d");
    EXPECT_TRUE(table.containsPath("/capio/d"));
    EXPECT_TRUE(table.fds().empty());
}

TEST(FileDescriptorTableTest, CopySharesTheOffsetButNotCloseOnExec) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);
    table.remove(3);

    EXPECT_FALSE(table.contains(3));
    EXPECT_TRUE(table.contains(4));
    EXPECT_TRUE(table.containsPath("/capio/f"));
    EXPECT_EQ(table.path(4), "/capio/f");
}

TEST(FileDescriptorTableTest, RemovingAnUnknownDescriptorDoesNothing) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.remove(4);
    EXPECT_EQ(table.fds(), std::vector<int>{3});
}

// The number of a descriptor that the kernel closed without CAPIO knowing is given again
TEST(FileDescriptorTableTest, AddingOntoATakenNumberReplacesIt) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/old", 0, 0, O_RDONLY, false);
    table.add(3, "/capio/new", 7, 0, O_WRONLY, false);

    EXPECT_EQ(table.path(3), "/capio/new");
    EXPECT_EQ(table.offset(3), 7);
    table.removePath("/capio/old"); // the old path does not list the number any more
    EXPECT_TRUE(table.contains(3));
    EXPECT_EQ(table.path(3), "/capio/new");
}

TEST(FileDescriptorTableTest, CopyOntoATakenNumberReplacesIt) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.add(4, "/capio/g", 0, 0, O_RDONLY, false);
    table.dup(3, 4, false);

    EXPECT_EQ(table.path(4), "/capio/f");
    EXPECT_TRUE(table.sharesOffset(3, 4));
    table.removePath("/capio/g");
    EXPECT_TRUE(table.contains(4));
}

TEST(FileDescriptorTableTest, RemovingAPathRemovesOnlyItsDescriptors) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.removePath("/capio/g");
    EXPECT_TRUE(table.contains(3));
}

TEST(FileDescriptorTableTest, RenamingAPathMovesItsDescriptors) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_RDONLY, false);
    table.renamePath("/capio/g", "/capio/h");

    EXPECT_FALSE(table.containsPath("/capio/h"));
    EXPECT_EQ(table.path(3), "/capio/f");
}

TEST(FileDescriptorTableTest, CloseClosesInTheKernelAndForgets) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);
    table.dup(3, 4, false);

    EXPECT_EQ(table.close(3), 0);
    EXPECT_EQ(system_calls.closed, std::vector<int>{3});
    EXPECT_FALSE(table.contains(3));
    EXPECT_TRUE(table.contains(4));
}

// Linux frees the number also when close fails, e.g. with an I/O error on NFS
TEST(FileDescriptorTableTest, FailedCloseForgetsTooAndReturnsTheError) {
    RecordingSystemCalls system_calls;
    system_calls.result = -EIO;
    Table table(system_calls);
    table.add(3, "/capio/f", 0, 0, O_WRONLY, false);

    EXPECT_EQ(table.close(3), -EIO);
    EXPECT_FALSE(table.contains(3));
}

// No other thread sees the number in the table while the kernel closes it
TEST(FileDescriptorTableTest, CloseHoldsTheTableUntilTheKernelHasClosed) {
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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
    Table::protectAcrossFork();
    RecordingSystemCalls system_calls;
    Table table(system_calls);
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

#endif // CAPIO_TEST_UNIT_POSIX_FILE_DESCRIPTOR_TABLE_HPP

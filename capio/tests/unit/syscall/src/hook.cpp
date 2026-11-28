#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "early_thread.hpp"
#include "helpers.hpp"

/*
 * The hook of libcapio_posix.cpp: what it does with a system call before, or instead of, giving it
 * to a handler.
 */

TEST(HookTest, ASystemCallNumberPastTheTableGoesToTheKernel) {
    // Larger than any system call CAPIO handles: the kernel answers, and it has no such call
    errno = 0;
    EXPECT_EQ(syscall(100000), -1);
    EXPECT_EQ(errno, ENOSYS);
}

TEST(HookTest, AnExceptionInAHandlerEndsTheProcess) {
    // A call that CAPIO does not implement on a CAPIO file throws in its handler: the hook prints
    // a report and exits with EXIT_FAILURE, instead of letting the exception unwind into libc
    constexpr const char *PATHNAME = "hook_unimplemented.txt";
    write_file(PATHNAME, "data");
    const int status = run_in_child([PATHNAME] {
        const int fd = open(PATHNAME, O_RDWR);
        if (fd < 0) {
            return 2;
        }
        if (memory_variant()) {
            sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WRITE);
        } else {
            chmod(PATHNAME, S_IRUSR | S_IWUSR);
        }
        return 0; // not reached
    });
    EXPECT_EQ(status, EXIT_FAILURE);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(HookTest, AThreadStartedBeforeLibcapioIsSetUpAtItsFirstSystemCall) {
    // The thread of early_thread.cpp never went through the clone hook: its first system call
    // after the constructor of libcapio sets it up, and it then reads a CAPIO file
    constexpr const char *PATHNAME = "hook_early_thread.txt";
    write_file(PATHNAME, "read by an early thread");
    char buffer[64]{};
    EXPECT_EQ(capio_test_early_thread_read(PATHNAME, buffer, sizeof(buffer)),
              static_cast<long>(std::strlen("read by an early thread")));
    EXPECT_STREQ(buffer, "read by an early thread");
    EXPECT_EQ(capio_test_early_thread_read(PATHNAME, buffer, sizeof(buffer)), -EALREADY);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

#if defined(SYS_sync_file_range)
TEST(HookTest, SyncFileRangeOfAFileNotOfCapioGoesToTheKernel) {
    char path[]  = "/tmp/sync_file_range_test_XXXXXX"; // outside CAPIO_DIR, /tmp/capio in Docker
    const int fd = mkstemp(path);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(write(fd, "data", 4), 4);
    EXPECT_EQ(sync_file_range(fd, 0, 4, SYNC_FILE_RANGE_WRITE), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(path), 0);
}

TEST(HookTest, SyncFileRangeOfACapioFile) {
    if (memory_variant()) {
        // Not implemented: its descriptor is of /dev/null (see AnExceptionInAHandlerEndsTheProcess)
        GTEST_SKIP() << "the memory variant does not implement sync_file_range";
    }
    constexpr const char *PATHNAME = "hook_sync_file_range.txt";
    write_file(PATHNAME, "data");
    const int fd = open(PATHNAME, O_RDWR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(sync_file_range(fd, 0, 4, SYNC_FILE_RANGE_WRITE), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}
#endif

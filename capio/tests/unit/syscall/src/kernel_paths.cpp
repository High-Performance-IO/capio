#include <gtest/gtest.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "helpers.hpp"

/*
 * What the handlers give to the kernel without asking the server: forbidden paths (/proc, /sys),
 * relative paths that do not resolve from a working directory outside CAPIO_DIR, descriptors that
 * are not directories or not open, and files that are not of CAPIO. The kernel's answer is the
 * answer: these tests check that CAPIO lets it through, in both variants.
 */

namespace {

/// @brief A file outside CAPIO_DIR (/tmp/capio in Docker: not a name that starts with it)
class KernelPathsTest : public testing::Test {
  protected:
    char kernel_file[64] = "/tmp/kernel_paths_test_XXXXXX";
    int kernel_fd        = -1;
    char capio_cwd[PATH_MAX]{};

    void SetUp() override {
        kernel_fd = mkstemp(kernel_file);
        ASSERT_NE(kernel_fd, -1) << std::strerror(errno);
        ASSERT_NE(getcwd(capio_cwd, sizeof(capio_cwd)), nullptr);
    }

    void TearDown() override {
        EXPECT_EQ(chdir(capio_cwd), 0);
        close(kernel_fd);
        unlink(kernel_file);
    }

    /// @brief Work from "/", outside CAPIO_DIR, so that relative paths are not CAPIO paths
    static void leave_capio_dir() { ASSERT_EQ(chdir("/"), 0); }
};

constexpr const char *MISSING_PROC = "/proc/self/capio_missing";
constexpr const char *MISSING_REL  = "capio_kernel_paths_missing/child";
constexpr int CLOSED_FD            = 9999;

} // namespace

TEST_F(KernelPathsTest, ForbiddenPathsGoToTheKernel) {
    errno = 0;
    EXPECT_EQ(open(MISSING_PROC, O_RDONLY), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(access(MISSING_PROC, F_OK), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(mkdir("/proc/self/capio_missing_dir", 0700), -1);
    EXPECT_EQ(rmdir("/proc/self/capio_missing_dir"), -1);
    EXPECT_EQ(unlink(MISSING_PROC), -1);
    EXPECT_EQ(chmod(MISSING_PROC, S_IRUSR), -1);
    EXPECT_EQ(chdir("/proc/self/capio_missing_dir"), -1);
    EXPECT_EQ(errno, ENOENT);

    struct statx info{};
    EXPECT_EQ(statx(AT_FDCWD, MISSING_PROC, 0, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(statx(AT_FDCWD, "/proc/self/status", 0, STATX_BASIC_STATS, &info), 0);

    // The token files of the file system monitor of CAPIO-CL are not files of the workflow
    constexpr const char *TOKEN = ".kernel_paths.commit";
    const int fd                = open(TOKEN, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(close(fd), 0);
    struct stat kernel_info{};
    EXPECT_EQ(stat(TOKEN, &kernel_info), 0); // on the file system, in both variants
    EXPECT_EQ(unlink(TOKEN), 0);
}

TEST_F(KernelPathsTest, RelativePathsThatDoNotResolveOutsideCapioDirGoToTheKernel) {
    leave_capio_dir();
    char cwd[PATH_MAX]{};
    EXPECT_STREQ(getcwd(cwd, sizeof(cwd)), "/"); // not a CAPIO path: the kernel answers

    errno = 0;
    EXPECT_EQ(open(MISSING_REL, O_RDONLY), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(access(MISSING_REL, F_OK), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(mkdir(MISSING_REL, 0700), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(rmdir(MISSING_REL), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(unlink(MISSING_REL), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(chmod(MISSING_REL, S_IRUSR), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(chdir(MISSING_REL), -1);
    EXPECT_EQ(errno, ENOENT);

    struct statx info{};
    EXPECT_EQ(statx(AT_FDCWD, MISSING_REL, 0, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST_F(KernelPathsTest, PathsOutsideCapioDirGoToTheKernel) {
    struct statx info{};
    EXPECT_EQ(statx(AT_FDCWD, kernel_file, 0, STATX_BASIC_STATS, &info), 0);
    EXPECT_EQ(info.stx_size, 0U);
    EXPECT_EQ(access(kernel_file, R_OK | W_OK), 0);
    EXPECT_EQ(chmod(kernel_file, S_IRUSR | S_IWUSR), 0);

    const std::string directory = std::string(kernel_file) + "_dir";
    EXPECT_EQ(mkdir(directory.c_str(), 0700), 0);
    EXPECT_EQ(rmdir(directory.c_str()), 0);
}

TEST_F(KernelPathsTest, ADescriptorThatIsNotADirectoryGoesToTheKernel) {
    // Relative to a regular file: ENOTDIR, from the kernel or from CAPIO
    errno = 0;
    EXPECT_EQ(openat(kernel_fd, "child", O_RDONLY), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(faccessat(kernel_fd, "child", F_OK, 0), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(mkdirat(kernel_fd, "child", 0700), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(unlinkat(kernel_fd, "child", 0), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(fchmodat(kernel_fd, "child", S_IRUSR, 0), -1);
    EXPECT_EQ(errno, ENOTDIR);

    struct statx info{};
    EXPECT_EQ(statx(kernel_fd, "child", 0, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, ENOTDIR);
}

TEST_F(KernelPathsTest, ADescriptorThatIsNotOpenGoesToTheKernel) {
    errno = 0;
    EXPECT_EQ(openat(CLOSED_FD, "child", O_RDONLY), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(faccessat(CLOSED_FD, "child", F_OK, 0), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(mkdirat(CLOSED_FD, "child", 0700), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(unlinkat(CLOSED_FD, "child", 0), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(fchmodat(CLOSED_FD, "child", S_IRUSR, 0), -1);
    EXPECT_EQ(errno, EBADF);

    struct statx info{};
    EXPECT_EQ(statx(CLOSED_FD, "child", 0, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(statx(CLOSED_FD, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(KernelPathsTest, TheFileOfADescriptorNotOfCapio) {
    struct statx info{};
    EXPECT_EQ(statx(kernel_fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &info), 0);
    EXPECT_EQ(fchownat(kernel_fd, "", getuid(), getgid(), AT_EMPTY_PATH), 0);

    struct statfs file_system{};
    EXPECT_EQ(fstatfs(kernel_fd, &file_system), 0);

    EXPECT_EQ(lseek(kernel_fd, 0, SEEK_SET), 0);
    char first[] = "ab", second[] = "cd";
    struct iovec iov[2] = {{first, 2}, {second, 2}};
    EXPECT_EQ(writev(kernel_fd, iov, 2), 4);

    const int copy = dup(kernel_fd);
    ASSERT_NE(copy, -1);
    EXPECT_EQ(close(copy), 0);
}

TEST_F(KernelPathsTest, TheEntriesOfADirectoryNotOfCapio) {
    const int fd = open("/", O_RDONLY | O_DIRECTORY);
    ASSERT_NE(fd, -1);
    alignas(8) char buffer[4096];
    EXPECT_GT(syscall(SYS_getdents64, fd, buffer, sizeof(buffer)), 0);
    EXPECT_EQ(close(fd), 0);
}

TEST_F(KernelPathsTest, StatxWithANullPathGoesToTheKernel) {
    struct statx info{};
    errno = 0;
    EXPECT_EQ(syscall(SYS_statx, AT_FDCWD, nullptr, 0, STATX_BASIC_STATS, &info), -1);
    EXPECT_EQ(errno, EFAULT);
}

TEST_F(KernelPathsTest, StatxOfTheWorkingDirectoryWithAnEmptyPath) {
    // AT_FDCWD with AT_EMPTY_PATH: the working directory itself, CAPIO_DIR here
    struct statx info{};
    EXPECT_EQ(statx(AT_FDCWD, "", AT_EMPTY_PATH, STATX_BASIC_STATS, &info), 0);
    EXPECT_TRUE(S_ISDIR(info.stx_mode));
}

#ifdef SYS_open
TEST_F(KernelPathsTest, TheOpenSystemCall) {
    // glibc's open() issues openat: SYS_open is the call of older programs, and of x86-64 only
    constexpr const char *PATHNAME = "kernel_paths_sys_open.txt";
    const auto fd                  = static_cast<int>(
        syscall(SYS_open, PATHNAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR));
    ASSERT_NE(fd, -1) << std::strerror(errno);
    EXPECT_EQ(write(fd, "data", 4), 4);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(read_file(PATHNAME), "data");
    EXPECT_EQ(syscall(SYS_open, MISSING_PROC, O_RDONLY), -1);
    EXPECT_EQ(errno, ENOENT);
    EXPECT_EQ(unlink(PATHNAME), 0);
}
#endif

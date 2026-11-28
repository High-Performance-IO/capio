#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "helpers.hpp"

/*
 * Calls on CAPIO descriptors and paths that the other files do not cover: extended attributes,
 * close_range, and dup2 of a descriptor of the kernel onto a CAPIO one.
 */

TEST(DescriptorsTest, ACapioFileHasNoExtendedAttributes) {
    constexpr const char *PATHNAME = "descriptors_xattr.txt";
    write_file(PATHNAME, "data");
    const int fd = open(PATHNAME, O_RDONLY);
    ASSERT_NE(fd, -1);
    char value[64];
    errno                   = 0;
    // The memory variant keeps none: ENOTSUP. The FS variant asks the kernel, which has none of
    // this name (ENODATA), or no user attributes on this file system (ENOTSUP).
    const auto no_attribute = [](const int error) { return error == ENOTSUP || error == ENODATA; };
    EXPECT_EQ(getxattr(PATHNAME, "user.capio_missing", value, sizeof(value)), -1);
    EXPECT_PRED1(no_attribute, errno);
    EXPECT_EQ(lgetxattr(PATHNAME, "user.capio_missing", value, sizeof(value)), -1);
    EXPECT_PRED1(no_attribute, errno);

    char list[256];
    const ssize_t listed = flistxattr(fd, list, sizeof(list));
    if (memory_variant()) {
        EXPECT_EQ(listed, -1);
        EXPECT_EQ(errno, ENOTSUP);
    } else {
        EXPECT_GE(listed, 0);
    }
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(DescriptorsTest, TheExtendedAttributesOfAFileNotOfCapioComeFromTheKernel) {
    char path[]  = "/tmp/descriptors_xattr_XXXXXX"; // outside CAPIO_DIR, /tmp/capio in Docker
    const int fd = mkstemp(path);
    ASSERT_NE(fd, -1);
    char value[64];
    EXPECT_EQ(getxattr(path, "user.capio_missing", value, sizeof(value)), -1);
    EXPECT_EQ(lgetxattr(path, "user.capio_missing", value, sizeof(value)), -1);
    char list[256];
    EXPECT_GE(flistxattr(fd, list, sizeof(list)), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(path), 0);
}

TEST(DescriptorsTest, TheOwnerOfACapioDescriptorWithAnEmptyPath) {
    if (!memory_variant()) {
        GTEST_SKIP() << "the FS variant does not implement chown of CAPIO files yet";
    }
    constexpr const char *PATHNAME = "descriptors_fchownat.txt";
    write_file(PATHNAME, "data");
    const int fd = open(PATHNAME, O_RDONLY);
    ASSERT_NE(fd, -1);
    // The target is the file of the descriptor itself. CAPIO keeps no owner: nothing changes.
    EXPECT_EQ(fchownat(fd, "", getuid(), getgid(), AT_EMPTY_PATH), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

#ifdef SYS_close_range
TEST(DescriptorsTest, CloseRangeWithoutCapioDescriptorsGoesToTheKernel) {
    const int fd = open("/dev/null", O_RDONLY);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(syscall(SYS_close_range, fd, fd, 0), 0);
    EXPECT_EQ(fcntl(fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(DescriptorsTest, CloseRangeOfCapioDescriptors) {
    constexpr const char *PATHNAME = "descriptors_close_range.txt";
    write_file(PATHNAME, "data");
    if (memory_variant()) {
        // Not implemented: the hook ends the process, so in a child
        EXPECT_EQ(run_in_child([PATHNAME] {
                      const int fd = open(PATHNAME, O_RDONLY);
                      return fd < 0 ? 2 : static_cast<int>(syscall(SYS_close_range, fd, fd, 0));
                  }),
                  EXIT_FAILURE);
        EXPECT_EQ(unlink(PATHNAME), 0);
        return;
    }

    const int cloexec = open(PATHNAME, O_RDONLY);
    ASSERT_NE(cloexec, -1);
    EXPECT_EQ(syscall(SYS_close_range, cloexec, cloexec, CLOSE_RANGE_CLOEXEC), 0);
    EXPECT_EQ(fcntl(cloexec, F_GETFD), FD_CLOEXEC); // still open
    EXPECT_EQ(close(cloexec), 0);

    const int written = open(PATHNAME, O_WRONLY | O_APPEND);
    ASSERT_NE(written, -1);
    EXPECT_EQ(write(written, "more", 4), 4);
    EXPECT_EQ(syscall(SYS_close_range, written, written, 0), 0);
    EXPECT_EQ(fcntl(written, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(read_file(PATHNAME), "datamore");
    EXPECT_EQ(unlink(PATHNAME), 0);
}
#endif

TEST(DescriptorsTest, Dup2OfAKernelDescriptorOntoACapioOne) {
    constexpr const char *PATHNAME = "descriptors_dup2.txt";
    write_file(PATHNAME, "capio");
    char kernel_path[] = "/tmp/descriptors_dup2_XXXXXX";
    const int kernel   = mkstemp(kernel_path);
    ASSERT_NE(kernel, -1);
    ASSERT_EQ(write(kernel, "kernel", 6), 6);
    ASSERT_EQ(lseek(kernel, 0, SEEK_SET), 0);

    const int capio = open(PATHNAME, O_RDONLY);
    ASSERT_NE(capio, -1);
    // The CAPIO descriptor is replaced: it is now the kernel's file, and CAPIO forgets it
    EXPECT_EQ(dup2(kernel, capio), capio);
    char buffer[16]{};
    EXPECT_EQ(read(capio, buffer, sizeof(buffer)), 6);
    EXPECT_STREQ(buffer, "kernel");
    EXPECT_EQ(close(capio), 0);

    // From a descriptor that is not open: nothing is replaced
    const int other = open(PATHNAME, O_RDONLY);
    ASSERT_NE(other, -1);
    errno = 0;
    EXPECT_EQ(dup2(9999, other), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(read(other, buffer, sizeof(buffer)), 5); // still the CAPIO file
    EXPECT_EQ(close(other), 0);

    EXPECT_EQ(close(kernel), 0);
    EXPECT_EQ(unlink(kernel_path), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(DescriptorsTest, ReadAtAnOffsetOfACapioFile) {
    constexpr const char *PATHNAME = "descriptors_pread.txt";
    write_file(PATHNAME, "0123456789");
    if (memory_variant()) {
        // Not implemented: the hook ends the process, so in children
        for (const int call : {0, 1}) {
            EXPECT_EQ(run_in_child([PATHNAME, call] {
                          const int fd = open(PATHNAME, O_RDONLY);
                          char buffer[4];
                          struct iovec iov = {buffer, sizeof(buffer)};
                          return fd < 0      ? 2
                                 : call == 0 ? static_cast<int>(pread(fd, buffer, 4, 2))
                                             : static_cast<int>(preadv(fd, &iov, 1, 2));
                      }),
                      EXIT_FAILURE);
        }
        EXPECT_EQ(unlink(PATHNAME), 0);
        return;
    }
    // FS variant: CAPIO waits for the bytes, and the kernel reads them
    const int fd = open(PATHNAME, O_RDONLY);
    ASSERT_NE(fd, -1);
    char buffer[8]{};
    EXPECT_EQ(pread(fd, buffer, 4, 2), 4);
    EXPECT_STREQ(buffer, "2345");
    char first[3]{}, second[3]{};
    struct iovec iov[2] = {{first, 2}, {second, 2}};
    EXPECT_EQ(preadv(fd, iov, 2, 6), 4);
    EXPECT_STREQ(first, "67");
    EXPECT_STREQ(second, "89");
    EXPECT_EQ(preadv2(fd, iov, 1, -1, 0), 2); // -1: at the file offset, which is still 0
    EXPECT_STREQ(first, "01");
    errno = 0;
    EXPECT_EQ(pread(fd, buffer, 4, -1), -1); // a negative offset, refused by the kernel
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(DescriptorsTest, ReadvSkipsEmptyBuffers) {
    constexpr const char *PATHNAME = "descriptors_readv.txt";
    write_file(PATHNAME, "abcdef");
    const int fd = open(PATHNAME, O_RDONLY);
    ASSERT_NE(fd, -1);
    char first[4]{}, unused[1]{}, second[4]{};
    struct iovec iov[3] = {{first, 3}, {unused, 0}, {second, 3}};
    EXPECT_EQ(readv(fd, iov, 3), 6);
    EXPECT_STREQ(first, "abc");
    EXPECT_STREQ(second, "def");
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(DescriptorsTest, ModeAndOwnerOfADescriptorNotOfCapioComeFromTheKernel) {
    char path[]  = "/tmp/descriptors_fchmod_XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(fchmod(fd, S_IRUSR), 0);
    struct stat info{};
    EXPECT_EQ(fstat(fd, &info), 0);
    EXPECT_EQ(info.st_mode & 0777, S_IRUSR);
    EXPECT_EQ(fchown(fd, getuid(), getgid()), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(path), 0);
}

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// copy_file_range on a CAPIO file is done by CAPIO, with the same answer as the kernel: the data,
// the offsets and the errors.

namespace {

std::string read_whole_file(const char *path) {
    auto fd = open(path, O_RDONLY);
    EXPECT_GE(fd, 0);
    std::string content;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        content.append(buf, n);
    }
    EXPECT_EQ(n, 0);
    EXPECT_EQ(close(fd), 0);
    return content;
}

int create_file_with(const char *path, const std::string &content, int flags = O_RDWR) {
    auto fd = open(path, O_CREAT | O_TRUNC | flags, 0777);
    EXPECT_GE(fd, 0);
    EXPECT_EQ(write(fd, content.data(), content.size()), static_cast<ssize_t>(content.size()));
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    return fd;
}

// A path in the temporary directory, which the kernel handles when it is not inside CAPIO_DIR.
std::filesystem::path posix_path(const char *name) {
    auto capio_dir = std::filesystem::weakly_canonical(std::getenv("CAPIO_DIR"));
    auto tmp_dir   = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    auto relative  = tmp_dir.lexically_relative(capio_dir);
    if (!relative.empty() && *relative.begin() != "..") {
        return {};
    }
    return tmp_dir / (std::string(name) + "." + std::to_string(getpid()));
}

} // namespace

TEST(SystemCallTest, TestCopyFileRangeBetweenCapioFiles) {
    auto in  = create_file_with("in.txt", "abcdefgh");
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 8, 0), 8);

    // without offsets, both descriptor offsets move past the bytes copied
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 8);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 8);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), "abcdefgh");
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeStartsFromTheDescriptorOffsets) {
    auto in  = create_file_with("in.txt", "abcdefgh");
    auto out = create_file_with("out.txt", "12345678");
    EXPECT_EQ(lseek(in, 2, SEEK_SET), 2);
    EXPECT_EQ(lseek(out, 4, SEEK_SET), 4);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 3, 0), 3);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 5);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 7);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), "1234cde8");
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithOffsetsLeavesTheDescriptorOffsets) {
    auto in       = create_file_with("in.txt", "abcdefgh");
    auto out      = create_file_with("out.txt", "12345678");
    loff_t off_in = 2, off_out = 1;
    EXPECT_EQ(copy_file_range(in, &off_in, out, &off_out, 4, 0), 4);

    // the offsets passed are advanced, the descriptor offsets stay where they were
    EXPECT_EQ(off_in, 6);
    EXPECT_EQ(off_out, 5);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 0);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 0);

    // and the next read and write happen at the descriptor offsets
    char buf[2];
    EXPECT_EQ(read(in, buf, 2), 2);
    EXPECT_EQ(memcmp(buf, "ab", 2), 0);
    EXPECT_EQ(write(out, "X", 1), 1);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), "Xcdef678");
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeStopsAtTheEndOfTheFile) {
    auto in  = create_file_with("in.txt", "abcd");
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 100, 0), 4);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 100, 0), 0);
    loff_t off_in = 10;
    EXPECT_EQ(copy_file_range(in, &off_in, out, nullptr, 100, 0), 0);
    EXPECT_EQ(off_in, 10);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), "abcd");
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeOfZeroBytes) {
    auto in  = create_file_with("in.txt", "abcd");
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 0, 0), 0);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 0);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 0);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeOfAFileBiggerThanTheCopyBuffer) {
    // many times the buffer the copy goes through, a CAPIO cache line, and not a multiple of it
    std::string content(5 * 1024 * 1024 + 123, '\0');
    for (size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<char>('a' + i % 26);
    }
    auto in  = create_file_with("in.txt", content);
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, content.size(), 0),
              static_cast<ssize_t>(content.size()));
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), content);
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithinTheSameFile) {
    auto fd        = create_file_with("test.txt", "abcd");
    loff_t off_in  = 0;
    loff_t off_out = 4;
    EXPECT_EQ(copy_file_range(fd, &off_in, fd, &off_out, 4, 0), 4);
    EXPECT_EQ(off_in, 4);
    EXPECT_EQ(off_out, 8);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(read_whole_file("test.txt"), "abcdabcd");
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithOverlappingRangesFailsWithEinval) {
    auto fd        = create_file_with("test.txt", "abcdefgh");
    loff_t off_in  = 0;
    loff_t off_out = 2;
    EXPECT_EQ(copy_file_range(fd, &off_in, fd, &off_out, 4, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(off_in, 0);
    EXPECT_EQ(off_out, 2);

    // a duplicate names the same file
    auto dup_fd = dup(fd);
    EXPECT_EQ(copy_file_range(fd, &off_in, dup_fd, &off_out, 4, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(dup_fd), 0);

    // only the data in the file counts: past its end, the ranges do not overlap
    off_in  = 8;
    off_out = 9;
    EXPECT_EQ(copy_file_range(fd, &off_in, fd, &off_out, 4, 0), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(read_whole_file("test.txt"), "abcdefgh");
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithFlagsFailsWithEinval) {
    auto in  = create_file_with("in.txt", "abcd");
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 4, 1), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithANegativeOffset) {
    auto in        = create_file_with("in.txt", "abcd");
    auto out       = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    loff_t off_in  = -1;
    loff_t off_out = -1;

    // as an unsigned offset, the range wraps past the largest offset
    EXPECT_EQ(copy_file_range(in, &off_in, out, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EOVERFLOW);
    EXPECT_EQ(copy_file_range(in, nullptr, out, &off_out, 4, 0), -1);
    EXPECT_EQ(errno, EOVERFLOW);

    // an empty range does not wrap, and the offset itself is invalid
    EXPECT_EQ(copy_file_range(in, &off_in, out, nullptr, 0, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(off_in, -1);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeWithTheWrongAccessModeFailsWithEbadf) {
    EXPECT_EQ(close(create_file_with("in.txt", "abcd")), 0);
    auto write_only = open("in.txt", O_WRONLY);
    auto read_only  = open("in.txt", O_RDONLY);
    auto out        = open("out.txt", O_CREAT | O_RDWR | O_TRUNC, 0777);
    auto append     = open("out.txt", O_WRONLY | O_APPEND);

    // the source must be readable
    EXPECT_EQ(copy_file_range(write_only, nullptr, out, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);
    // the destination must be writable
    EXPECT_EQ(copy_file_range(out, nullptr, read_only, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);
    // and not open in append mode
    EXPECT_EQ(copy_file_range(read_only, nullptr, append, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);
    // a descriptor that is not open
    EXPECT_EQ(copy_file_range(read_only, nullptr, -1, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);

    // the O_ACCMODE access mode, for ioctl only, allows neither reading nor writing
    auto ioctl_only = open("in.txt", O_ACCMODE);
    EXPECT_GE(ioctl_only, 0);
    EXPECT_EQ(copy_file_range(ioctl_only, nullptr, out, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(copy_file_range(read_only, nullptr, ioctl_only, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EBADF);

    for (int fd : {write_only, read_only, out, append, ioctl_only}) {
        EXPECT_EQ(close(fd), 0);
    }
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink("in.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeOnADirectoryFailsWithEisdir) {
    EXPECT_EQ(mkdir("test_dir", 0777), 0);
    auto dir = open("test_dir", O_RDONLY | O_DIRECTORY);
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(dir, nullptr, out, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EISDIR);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(dir), 0);
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_NE(rmdir("test_dir"), -1);
}

TEST(SystemCallTest, TestCopyFileRangeWithAPipeFailsWithEinval) {
    int pipe_fds[2];
    EXPECT_EQ(pipe(pipe_fds), 0);
    EXPECT_EQ(write(pipe_fds[1], "abcd", 4), 4);
    auto out = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    EXPECT_EQ(copy_file_range(pipe_fds[0], nullptr, out, nullptr, 4, 0), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(pipe_fds[0]), 0);
    EXPECT_EQ(close(pipe_fds[1]), 0);
    EXPECT_EQ(unlink("out.txt"), 0);
}

TEST(SystemCallTest, TestCopyFileRangeFromAPosixFileIntoACapioFile) {
    auto path = posix_path("capio_copy_file_range_in");
    if (path.empty()) {
        GTEST_SKIP() << "the temporary directory is inside CAPIO_DIR";
    }
    auto in       = create_file_with(path.c_str(), "abcdefgh");
    auto out      = open("out.txt", O_CREAT | O_WRONLY | O_TRUNC, 0777);
    loff_t off_in = 4;
    EXPECT_EQ(copy_file_range(in, &off_in, out, nullptr, 100, 0), 4);
    EXPECT_EQ(off_in, 8);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 0);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 2, 0), 2);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 2);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file("out.txt"), "efghab");
    EXPECT_EQ(unlink("out.txt"), 0);
    EXPECT_EQ(unlink(path.c_str()), 0);
}

TEST(SystemCallTest, TestCopyFileRangeFromACapioFileIntoAPosixFile) {
    auto path = posix_path("capio_copy_file_range_out");
    if (path.empty()) {
        GTEST_SKIP() << "the temporary directory is inside CAPIO_DIR";
    }
    auto in        = create_file_with("in.txt", "abcdefgh");
    auto out       = create_file_with(path.c_str(), "12345678");
    loff_t off_out = 6;
    EXPECT_EQ(copy_file_range(in, nullptr, out, &off_out, 2, 0), 2);
    EXPECT_EQ(off_out, 8);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 2);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 0);
    EXPECT_EQ(copy_file_range(in, nullptr, out, nullptr, 3, 0), 3);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 3);
    EXPECT_EQ(close(out), 0);
    EXPECT_EQ(close(in), 0);
    EXPECT_EQ(read_whole_file(path.c_str()), "cde456ab");
    EXPECT_EQ(unlink("in.txt"), 0);
    EXPECT_EQ(unlink(path.c_str()), 0);
}

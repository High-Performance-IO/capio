#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

struct linux_dirent64 {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;    /* File type */
    char d_name[NAME_MAX];   /* Filename (null-terminated) */
};

TEST(SystemCallTest, TestDirentsOnCapioDir) {
    const std::filesystem::path PATHNAME  = "test";
    const std::filesystem::path PATHNAME1 = "test_rmdir";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    const std::filesystem::path path1 = PATHNAME / "file1.txt";
    const std::filesystem::path path2 = PATHNAME / "file2.txt";
    const std::filesystem::path path3 = PATHNAME / "file3.txt";

    EXPECT_NE(mkdir(PATHNAME.c_str(), S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME.c_str(), F_OK), 0);

    std::vector<std::string> expectedNames{5};
    expectedNames[0] = ".";
    expectedNames[1] = "..";
    expectedNames[2] = "file1.txt";
    expectedNames[3] = "file2.txt";
    expectedNames[4] = "file3.txt";

    // Setup of files in capio dir
    int file1 = open(path1.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(file1, -1);
    EXPECT_EQ(access(path1.c_str(), F_OK), 0);
    EXPECT_EQ(write(file1, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(file1), -1);

    int file2 = open(path2.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(file2, -1);
    EXPECT_EQ(access(path2.c_str(), F_OK), 0);
    EXPECT_EQ(write(file2, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(file2), -1);

    int file3 = open(path3.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(file3, -1);
    EXPECT_EQ(access(path3.c_str(), F_OK), 0);
    EXPECT_EQ(write(file3, BUFFER, strlen(BUFFER)), strlen(BUFFER));
    EXPECT_NE(close(file3), -1);

    // Test of dirents now
    int curr_dir_fd = open(PATHNAME.c_str(), O_RDONLY | O_DIRECTORY);
    EXPECT_NE(curr_dir_fd, -1);

    long nread = 0;
    char buf[1024]; // byte buffer for getdents64 output data
    while (true) {

        nread = syscall(SYS_getdents64, curr_dir_fd, buf, 1024);
        // on fail sys_getdents returns -1

        EXPECT_NE(nread, -1);
        if (nread == 0) {
            break;
        }

        for (long bpos = 0, i = 0; bpos < nread && i < 10; i++) {
            auto d = (struct linux_dirent64 *) (buf + bpos);

            EXPECT_NE(std::find(expectedNames.begin(), expectedNames.end(), d->d_name),
                      expectedNames.end());
            bpos += d->d_reclen;
        }
    }
    EXPECT_NE(unlink(path1.c_str()), -1);
    EXPECT_NE(unlink(path2.c_str()), -1);
    EXPECT_NE(unlink(path3.c_str()), -1);
    EXPECT_NE(unlinkat(AT_FDCWD, PATHNAME.c_str(), AT_REMOVEDIR), -1);
    EXPECT_NE(access(PATHNAME.c_str(), F_OK), 0);

    EXPECT_NE(mkdir(PATHNAME1.c_str(), S_IRWXU), -1);
    EXPECT_NE(rmdir(PATHNAME1.c_str()), -1);
}

// CAPIO registers a directory as mkdir names it. "d/.", as ls stats it, and "d/", as tar opens it,
// were looked up as they were written and not found.
TEST(SystemCallTest, TestDirectoryIsFoundThroughDotAndTrailingSlash) {
    ASSERT_EQ(mkdir("normal_dir", S_IRWXU), 0);
    int fd = open("normal_dir/a", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_EQ(close(fd), 0);

    struct stat statbuf{};
    EXPECT_EQ(stat("normal_dir/.", &statbuf), 0);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_EQ(stat("normal_dir/", &statbuf), 0);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    DIR *dir = opendir("normal_dir/.");
    ASSERT_NE(dir, nullptr);
    std::vector<std::string> names;
    while (const dirent *entry = readdir(dir)) {
        names.emplace_back(entry->d_name);
    }
    EXPECT_EQ(closedir(dir), 0);
    EXPECT_NE(std::find(names.begin(), names.end(), "a"), names.end());

    EXPECT_EQ(unlink("normal_dir/a"), 0);
    EXPECT_NE(rmdir("normal_dir"), -1);
}

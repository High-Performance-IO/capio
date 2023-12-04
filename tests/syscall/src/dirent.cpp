#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

struct linux_dirent {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;    /* File type */
    char d_name[];           /* Filename (null-terminated) */
};

struct linux_dirent64 {
    ino64_t d_ino;           /* 64-bit inode number */
    off64_t d_off;           /* 64-bit offset to next structure */
    unsigned short d_reclen; /* Size of this dirent */
    unsigned char d_type;    /* File type */
    char d_name[];           /* Filename (null-terminated) */
};

bool namesAreOk(const std::string &check, const std::vector<std::string> &options) {
    printf("Checking %s\n", check.c_str());
    return std::find(options.begin(), options.end(), check) != options.end();
}

TEST_CASE("Test dirents on capio dir", "[syscall]") {
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    constexpr const char *path1 = "file1.txt";
    constexpr const char *path2 = "file2.txt";
    constexpr const char *path3 = "file3.txt";

    std::vector<std::string> expectedNames{5};
    expectedNames[0] = ".";
    expectedNames[1] = "..";
    expectedNames[2] = path1;
    expectedNames[3] = path2;
    expectedNames[4] = path3;

    // Setup of files in capio dir
    int file1 = open(path1, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file1 != -1);
    REQUIRE(access(path1, F_OK) == 0);
    REQUIRE(write(file1, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file1) != -1);

    int file2 = open(path2, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file2 != -1);
    REQUIRE(access(path2, F_OK) == 0);
    REQUIRE(write(file2, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file2) != -1);

    int file3 = open(path3, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file3 != -1);
    REQUIRE(access(path3, F_OK) == 0);
    REQUIRE(write(file3, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file3) != -1);

    // Test of dirents now
    char current_dir[1024];
    getcwd(current_dir, 1024);
    int curr_dir_fd = open(current_dir, O_RDONLY | O_DIRECTORY);
    REQUIRE(curr_dir_fd != -1);

    long nread = 0;
    char buf[1024]; // byte buffer for getdents64 output data
    char d_type;
    while (true) {

        nread = syscall(SYS_getdents64, curr_dir_fd, buf, 1024);
        // on fail sys_getdents returns -1

        REQUIRE(nread != -1);

        if (nread == 0) {
            break;
        }

        for (size_t bpos = 0; bpos < nread;) {
            auto d = (struct linux_dirent64 *) (buf + bpos);
            printf("%8ld  ", d->d_ino);
            d_type = d->d_type;

            printf("%-10s(%d) ",
                   (d_type == DT_REG)    ? "regular"
                   : (d_type == DT_DIR)  ? "directory"
                   : (d_type == DT_FIFO) ? "FIFO"
                   : (d_type == DT_SOCK) ? "socket"
                   : (d_type == DT_LNK)  ? "symlink"
                   : (d_type == DT_BLK)  ? "block dev"
                   : (d_type == DT_CHR)  ? "char dev"
                                         : "???",
                   d_type);

            std::string name = d->d_name;
            // REQUIRE(namesAreOk(name, expectedNames));

            printf("%4d %10lld  %s\n", d->d_reclen, (long long) d->d_off, d->d_name);
            bpos += d->d_reclen;
        }
    }
}

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
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

bool namesAreOk(const std::string &check, const std::vector<std::string> &options) {
    printf("checking %s\n", check.c_str());
    return std::find(options.begin(), options.end(), check) != options.end();
}

TEST_CASE("Test dirents on capio dir", "[syscall]") {
    const std::filesystem::path PATHNAME = "test";
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    const std::filesystem::path path1 = PATHNAME / "file1.txt";
    const std::filesystem::path path2 = PATHNAME / "file2.txt";
    const std::filesystem::path path3 = PATHNAME / "file3.txt";

    REQUIRE(mkdir(PATHNAME.c_str(), S_IRWXU) != -1);
    REQUIRE(access(PATHNAME.c_str(), F_OK) == 0);

    std::vector<std::string> expectedNames{5};
    expectedNames[0] = ".";
    expectedNames[1] = "..";
    expectedNames[2] = "file1.txt";
    expectedNames[3] = "file2.txt";
    expectedNames[4] = "file3.txt";

    // Setup of files in capio dir
    int file1 = open(path1.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file1 != -1);
    REQUIRE(access(path1.c_str(), F_OK) == 0);
    REQUIRE(write(file1, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file1) != -1);

    int file2 = open(path2.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file2 != -1);
    REQUIRE(access(path2.c_str(), F_OK) == 0);
    REQUIRE(write(file2, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file2) != -1);

    int file3 = open(path3.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    REQUIRE(file3 != -1);
    REQUIRE(access(path3.c_str(), F_OK) == 0);
    REQUIRE(write(file3, BUFFER, strlen(BUFFER)) == strlen(BUFFER));
    REQUIRE(close(file3) != -1);

    // Test of dirents now
    int curr_dir_fd = open(PATHNAME.c_str(), O_RDONLY | O_DIRECTORY);
    REQUIRE(curr_dir_fd != -1);

    long nread = 0;
    char buf[1024]; // byte buffer for getdents64 output data
    while (true) {

        nread = syscall(SYS_getdents64, curr_dir_fd, buf, 1024);
        // on fail sys_getdents returns -1

        REQUIRE(nread != -1);
        if (nread == 0) {
            break;
        }

        for (size_t bpos = 0; bpos < nread;) {
            auto d = (struct linux_dirent64 *) (buf + bpos);
            //  printf("%8ld  ", d->d_ino);
            // d_type           = d->d_type;
            /* printf("%-10s(%d) ",
                               (d_type == DT_REG)    ? "regular"
                               : (d_type == DT_DIR)  ? "directory"
                               : (d_type == DT_FIFO) ? "FIFO"
                               : (d_type == DT_SOCK) ? "socket"
                               : (d_type == DT_LNK)  ? "symlink"
                               : (d_type == DT_BLK)  ? "block dev"
                               : (d_type == DT_CHR)  ? "char dev"
                                                     : "???",
                               d_type); */

            REQUIRE(namesAreOk(d->d_name, expectedNames));

            // printf("%4d %10lld  %s\n", d->d_reclen, (long long) d->d_off, d->d_name);
            bpos += d->d_reclen;
        }
    }
    REQUIRE(unlink(path1.c_str()) != -1);
    REQUIRE(unlink(path2.c_str()) != -1);
    REQUIRE(unlink(path3.c_str()) != -1);
    REQUIRE(unlink(PATHNAME.c_str()) != -1);
    //   REQUIRE(access(PATHNAME.c_str(), F_OK) != 0);
}
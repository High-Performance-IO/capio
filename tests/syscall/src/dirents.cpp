#include <catch2/catch_test_macros.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

TEST_CASE("Test dirents on capio dir set to /tmp/capio", "[syscall]") {
    constexpr const char *BUFFER =
        "QWERTYUIOPASDFGHJKLZXCVBNM1234567890qwertyuiopasdfghjklzxcvbnm\0";
    constexpr const char *path1 = "/tmp/capio/file1.txt";
    constexpr const char *path2 = "/tmp/capio/file2.txt";
    constexpr const char *path3 = "/tmp/capio/file3.txt";

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

    int capio_dir_fd = open("/tmp/capio", O_RDONLY | O_DIRECTORY);

    int nread = 0;
    char buf[1024]; // byte buffer for getdents64 output data
    while (true) {

        nread = static_cast<int>(syscall(SYS_getdents64, capio_dir_fd, buf, 1024));

        if (nread == 0) {
            break;
        }

        for (int bpos = 0; bpos < nread;) {
            auto d = (struct dirent64 *) (buf + bpos);
            printf("%8ld  ", d->d_ino);
            char d_type = *(buf + bpos + d->d_reclen - 1);
            printf("%-10s(%d) ", (d_type == DT_REG)    ? "regular"
                             : (d_type == DT_DIR)  ? "directory"
                             : (d_type == DT_FIFO) ? "FIFO"
                             : (d_type == DT_SOCK) ? "socket"
                             : (d_type == DT_LNK)  ? "symlink"
                             : (d_type == DT_BLK)  ? "block dev"
                             : (d_type == DT_CHR)  ? "char dev"
                                                   : "???", d_type);


            // check for file names being the ones expected
            bool namesOk = (strcmp(d->d_name, ".") == 0) ||
                           (strcmp(d->d_name, "..") == 0) ||
                           (strcmp(d->d_name, "file1.txt") == 0) ||
                           (strcmp(d->d_name, "file2.txt") == 0) ||
                           (strcmp(d->d_name, "file3.txt") == 0);
            REQUIRE(namesOk);

            printf("%4d %10lld  %s\n", d->d_reclen, (long long) d->d_off, d->d_name);
            bpos += d->d_reclen;
        }
    }
}
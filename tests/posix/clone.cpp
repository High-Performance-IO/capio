#include <fcntl.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>

constexpr int ARRAY_SIZE = 100;

int func(int *num) {
    REQUIRE(*num == 12345);
    return 0;
}

void write_file(int fd) {
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    REQUIRE(write(fd, array, ARRAY_SIZE * sizeof(int)) == ARRAY_SIZE * sizeof(int));
}

TEST_CASE("Test thread clone", "[posix]") {
    int *num = static_cast<int *>(malloc(sizeof(int)));
    *num     = 12345;
    std::thread t(func, num);
    t.join();
    free(num);
}

TEST_CASE("Test thread clone producer/consumer", "[posix]") {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_RDWR | O_TRUNC;
    int fd                         = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    REQUIRE(fd != -1);
    std::thread t(write_file, fd);
    t.join();
    REQUIRE(lseek(fd, 0, SEEK_SET) == 0);
    int num;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        REQUIRE(read(fd, &num, sizeof(int)) == sizeof(int));
        REQUIRE(num == i);
    }
    REQUIRE(read(fd, &num, sizeof(int)) == 0);
    REQUIRE(close(fd) != -1);
    REQUIRE(unlink(PATHNAME) != -1);
}

#include <catch2/catch_test_macros.hpp>

#include <thread>

#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

constexpr int ARRAY_SIZE = 100;

sem_t *sem;

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

int write_file_stat_clone(FILE *fp) {
    constexpr int ARRAY_SIZE = 100;
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    REQUIRE(fwrite(array, sizeof(int), ARRAY_SIZE, fp) == ARRAY_SIZE);
    REQUIRE(sem_post(sem) == 0);
    return 0;
}

TEST_CASE("Test thread clone", "[syscall]") {
    int *num = static_cast<int *>(malloc(sizeof(int)));
    *num     = 12345;
    std::thread t(func, num);
    t.join();
    free(num);
}

TEST_CASE("Test thread clone producer/consumer", "[syscall]") {
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

TEST_CASE("Test thread clone producer/consumer with stat", "[syscall]") {
    constexpr const char *PATHNAME = "test_file.txt";
    sem                            = static_cast<sem_t *>(malloc(sizeof(sem_t)));
    REQUIRE(sem_init(sem, 0, 0) == 0);
    FILE *fp = fopen(PATHNAME, "w+");
    REQUIRE(fp != nullptr);
    std::thread t1(write_file_stat_clone, fp);
    REQUIRE(sem_wait(sem) == 0);
    REQUIRE(fseek(fp, 0, SEEK_SET) != -1);
    int num;
    while (fread(&num, sizeof(int), 1, fp) != 0)
        ;
    REQUIRE(feof(fp) != 0);
    REQUIRE(fclose(fp) != EOF);
    REQUIRE(unlink(PATHNAME) != -1);
    t1.join();
}
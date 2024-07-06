#include <gtest/gtest.h>

#include <thread>

#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

constexpr int ARRAY_SIZE = 100;

sem_t *sem;

int func(const int *num) {
    EXPECT_EQ(*num, 12345);
    return 0;
}

void write_file(int fd) {
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    EXPECT_EQ(write(fd, array, ARRAY_SIZE * sizeof(int)), ARRAY_SIZE * sizeof(int));
}

int write_file_stat_clone(FILE *fp) {
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    EXPECT_EQ(fwrite(array, sizeof(int), ARRAY_SIZE, fp), ARRAY_SIZE);
    EXPECT_EQ(sem_post(sem), 0);
    return 0;
}

TEST(SystemCallTest, TestThreadClone) {
    int *num = static_cast<int *>(malloc(sizeof(int)));
    *num     = 12345;
    std::thread t(func, num);
    t.join();
    free(num);
}

TEST(SystemCallTest, TestThreadCloneProducerConsumer) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_RDWR | O_TRUNC;
    int fd                         = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    std::thread t(write_file, fd);
    t.join();
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    int num;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        EXPECT_EQ(read(fd, &num, sizeof(int)), sizeof(int));
        EXPECT_EQ(num, i);
    }
    EXPECT_EQ(read(fd, &num, sizeof(int)), 0);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
}

TEST(SystemCallTest, TestForkProducerConsumer) {
    constexpr const char *PATHNAME = "test_file.txt";
    int flags                      = O_CREAT | O_RDWR | O_TRUNC;
    int fd                         = open(PATHNAME, flags, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    pid_t tid = fork();

    if (tid == 0) {
        // child
        write_file(fd);
    } else {
        EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
        int num;
        for (int i = 0; i < ARRAY_SIZE; i++) {
            EXPECT_EQ(read(fd, &num, sizeof(int)), sizeof(int));
            EXPECT_EQ(num, i);
        }
        EXPECT_EQ(read(fd, &num, sizeof(int)), 0);
        EXPECT_NE(close(fd), -1);
        EXPECT_NE(unlink(PATHNAME), -1);
    }
}

TEST(SystemCallTest, TestThreadCloneProducerConsumerWithStat) {
    constexpr const char *PATHNAME = "test_file.txt";
    sem                            = static_cast<sem_t *>(malloc(sizeof(sem_t)));
    EXPECT_EQ(sem_init(sem, 0, 0), 0);
    FILE *fp = fopen(PATHNAME, "w+");
    EXPECT_NE(fp, nullptr);
    std::thread t1(write_file_stat_clone, fp);
    EXPECT_EQ(sem_wait(sem), 0);
    EXPECT_NE(fseek(fp, 0, SEEK_SET), -1);
    int num;
    while (fread(&num, sizeof(int), 1, fp) != 0)
        ;
    EXPECT_NE(feof(fp), 0);
    EXPECT_NE(fclose(fp), EOF);
    EXPECT_NE(unlink(PATHNAME), -1);
    t1.join();
}
#include <semaphore.h>

#include <catch2/catch_test_macros.hpp>

sem_t* sem;

int func(void* arg) {
    int* num = static_cast<int*>(arg);
    REQUIRE(*num == 12345);
    return 0;
}

int write_file(void* arg) {
    constexpr int ARRAY_SIZE = 100;
    FILE* fp = reinterpret_cast<FILE*>(arg);
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    REQUIRE(fwrite(array, sizeof(int), ARRAY_SIZE, fp) == ARRAY_SIZE);
    REQUIRE(sem_post(sem) == 0);
    return 0;
}

TEST_CASE("Test thread clone", "[posix]") {
    constexpr int STACK_SIZE = 65536;
    void* child_stack = malloc(STACK_SIZE);
    int* num = static_cast<int*>(malloc(sizeof(int)));
    *num = 12345;
    int flags = CLONE_THREAD | CLONE_SIGHAND | CLONE_FS | CLONE_VM | CLONE_FILES;
    REQUIRE(clone(&func, static_cast<char*>(child_stack) + STACK_SIZE, flags, static_cast<void*>(num)) != -1);
    free(num);
}

TEST_CASE("Test thread clone producer/consumer", "[posix]") {
    constexpr const char* PATHNAME = "test_file.txt";
    constexpr int STACK_SIZE = 65536;
    void* child_stack = malloc(STACK_SIZE);
    sem = static_cast<sem_t*>(malloc(sizeof(sem_t)));
    REQUIRE(sem_init(sem, 0, 0) == 0);
    FILE* fp = fopen(PATHNAME, "w+");
    REQUIRE(fp != nullptr);
    int flags = CLONE_THREAD | CLONE_SIGHAND | CLONE_FS | CLONE_VM | CLONE_FILES;
    REQUIRE(clone(&write_file, static_cast<char*>(child_stack) + STACK_SIZE, flags, static_cast<void*>(fp)) != -1);
    REQUIRE(sem_wait(sem) == 0);
    REQUIRE(fseek(fp, 0, SEEK_SET) != -1);
    int num;
    while(fread(&num, sizeof(int), 1, fp) != 0);
    REQUIRE(feof(fp) != 0);
    REQUIRE(fclose(fp) != EOF);
    free(child_stack);
}
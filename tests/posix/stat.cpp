#include <semaphore.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <thread>
sem_t* sem1;

int write_file_stat(FILE* fp) {
    constexpr int ARRAY_SIZE = 100;
    int array[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }
    REQUIRE(fwrite(array, sizeof(int), ARRAY_SIZE, fp) == ARRAY_SIZE);
    REQUIRE(sem_post(sem1) == 0);
    return 0;
}


TEST_CASE("Test stat", "[posix]") {
    constexpr const char* PATHNAME = "test_file.txt";
    sem1 = static_cast<sem_t*>(malloc(sizeof(sem_t)));
    REQUIRE(sem_init(sem1, 0, 0) == 0);
    FILE* fp = fopen(PATHNAME, "w+");
    REQUIRE(fp != nullptr);
    write_file_stat(fp);
    REQUIRE(sem_wait(sem1) == 0);
    REQUIRE(fseek(fp, 0, SEEK_SET) != -1);
    int num;
    while(fread(&num, sizeof(int), 1, fp) != 0);
    REQUIRE(feof(fp) != 0);
    REQUIRE(fclose(fp) != EOF);
    REQUIRE(unlink(PATHNAME) != -1);
}
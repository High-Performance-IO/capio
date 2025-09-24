#include "common.hpp"

int mapReduceFunction(char *sourcedirname, ssize_t sstart, ssize_t sfiles, char *destdirname,
                      ssize_t dstart, ssize_t dfiles, float percent, int *return_value) {
    struct timeval before, after;
    struct stat statbuf;
    char *dataptr       = NULL;
    size_t datalen      = 0;
    size_t datacapacity = 0;
    gettimeofday(&before, NULL);

    EXPECT_NE(stat(sourcedirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_GE(sstart, 0);
    EXPECT_GT(sfiles, 0);
    EXPECT_NE(stat(destdirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));
    EXPECT_GE(dstart, 0);
    EXPECT_GT(dfiles, 0);
    EXPECT_GT(percent, 0);
    EXPECT_LE(percent, 1);

    char filepath[2 * PATH_MAX]{0};
    // concatenating all files in memory (dataptr)
    for (int i = 0 + sstart; i < (sfiles + sstart); ++i) {
        sprintf(filepath, fmtin, sourcedirname, i);

        FILE *fp = fopen(filepath, "r");
        EXPECT_NE(fileno(fp), -1);

        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        EXPECT_NE(ptr, nullptr);

        dataptr = ptr;
        fclose(fp);
    }

    int r = writedata(dataptr, datalen, percent, destdirname, dstart, dfiles);
    free(dataptr);

    *return_value = r;

    gettimeofday(&after, NULL);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "MAPREDUCE: elapsed time (ms) : %g\n", elapsed_time);

    return 0;
}

TEST(integrationTests, RunMapReducerTest) {
    int ret1 = -1, ret2 = -1;
    std::thread mapReducer1(mapReduceFunction, std::getenv("CAPIO_DIR"), 0, 5,
                            std::getenv("CAPIO_DIR"), 0, 5, 0.3, &ret1);
    std::thread mapReducer2(mapReduceFunction, std::getenv("CAPIO_DIR"), 1, 5,
                            std::getenv("CAPIO_DIR"), 1, 5, 0.3, &ret2);

    mapReducer1.join();
    mapReducer2.join();

    EXPECT_EQ(ret1, 0);
    EXPECT_EQ(ret2, 0);
}

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    // Destroy CAPIO SERVER and recreate it between each test execution
    // to provide a clean environment for each test
    testing::TestEventListeners &listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new CapioEnvironmentHandler(envp));

    return RUN_ALL_TESTS();
}

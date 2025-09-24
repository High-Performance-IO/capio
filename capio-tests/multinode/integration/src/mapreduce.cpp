#include "common.hpp"

int mapReduceFunction(const char *sourcedirname, ssize_t sstart, ssize_t sfiles,
                      const char *destdirname, ssize_t dstart, ssize_t dfiles, float percent,
                      int *return_value) {
    struct timeval before{}, after{};
    struct stat statbuf{};

    gettimeofday(&before, nullptr);

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

    std::vector<char> merged;

    char filepath[2 * PATH_MAX]{0};
    // Concatenate all files into `merged`
    for (int i = sstart; i < (sstart + sfiles); ++i) {
        sprintf(filepath, fmtin, sourcedirname, i);

        FILE *fp = fopen(filepath, "r");
        EXPECT_TRUE(fp != nullptr);

        auto chunk = readdata_new(fp);
        EXPECT_FALSE(chunk.empty());

        merged.insert(merged.end(), chunk.begin(), chunk.end());

        fclose(fp);
    }

    // Call writedata with the concatenated buffer
    int r = writedata(merged, percent, destdirname, dstart, dfiles);

    *return_value = r;

    gettimeofday(&after, nullptr);
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

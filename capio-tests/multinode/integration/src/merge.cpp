#include "common.hpp"

#include <linux/limits.h>

int mergeFunction(ssize_t nfiles, const char *sourcedir, const char *destdir) {
    timeval before{}, after{};
    struct stat statbuf{};

    gettimeofday(&before, nullptr);

    EXPECT_GT(nfiles, 0);

    EXPECT_NE(stat(sourcedir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    EXPECT_NE(stat(destdir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    std::vector<char> merged; // final result buffer

    char filepath[2 * PATH_MAX]{0};
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtout, sourcedir, i);
        FILE *fp = fopen(filepath, "r");
        EXPECT_TRUE(fp != nullptr);

        auto chunk = readdata(fp);
        EXPECT_FALSE(chunk.empty());

        // append this file's data to merged
        merged.insert(merged.end(), chunk.begin(), chunk.end());

        fclose(fp);
    }

    char resultpath[2 * PATH_MAX]{0};
    sprintf(resultpath, "%s/result.dat", destdir);
    FILE *fp = fopen(resultpath, "w");
    EXPECT_TRUE(fp);

    EXPECT_EQ(fwrite(merged.data(), 1, merged.size(), fp), merged.size());

    fclose(fp);

    gettimeofday(&after, nullptr);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "MERGE: elapsed time (ms) : %g\n", elapsed_time);

    return 0;
}

TEST(integrationTests, RunMergeProcess) {
    EXPECT_EQ(mergeFunction(2, std::getenv("CAPIO_DIR"), std::getenv("CAPIO_DIR")), 0);
}

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    // Destroy CAPIO SERVER and recreate it between each test execution
    // to provide a clean environment for each test
    testing::TestEventListeners &listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new CapioEnvironmentHandler(envp));

    return RUN_ALL_TESTS();
}

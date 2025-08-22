#include "common.hpp"

#include <linux/limits.h>

int mergeFunction(ssize_t nfiles, char *sourcedir, char *destdir) {

    struct timeval before, after;
    struct stat statbuf;
    char *dataptr  = NULL;
    size_t datalen = 0, datacapacity = 0;
    gettimeofday(&before, NULL);

    EXPECT_GT(nfiles, 0);

    EXPECT_NE(stat(sourcedir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    EXPECT_NE(stat(destdir, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode));

    char filepath[2 * PATH_MAX]{0};
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtout, sourcedir, i);
        FILE *fp = fopen(filepath, "r");
        EXPECT_TRUE(fp);

        char *ptr = readdata(fp, dataptr, &datalen, &datacapacity);
        EXPECT_NE(ptr, nullptr);

        dataptr = ptr;
        fclose(fp);
    }

    char resultpath[2 * PATH_MAX]{0};
    sprintf(resultpath, "%s/result.dat", destdir);
    FILE *fp = fopen(resultpath, "w");
    EXPECT_TRUE(fp);

    EXPECT_EQ(fwrite(dataptr, 1, datalen, fp), datalen);

    free(dataptr);

    gettimeofday(&after, NULL);
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

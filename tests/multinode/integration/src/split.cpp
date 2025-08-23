#include "common.hpp"

int splitFunction(ssize_t nlines, ssize_t nfiles, char *dirname) {
    struct timeval before, after;
    gettimeofday(&before, NULL);
    EXPECT_GT(nlines, 0);
    EXPECT_GT(nfiles, 0);

    // sanity check
    EXPECT_LE(nfiles, maxnumfiles);

    struct stat statbuf;
    EXPECT_NE(stat(dirname, &statbuf), -1);
    EXPECT_TRUE(S_ISDIR(statbuf.st_mode)); // not a directory

    FILE **fp = (FILE **) calloc(sizeof(FILE *), nfiles);
    EXPECT_TRUE(fp);

    char **buffer = (char **) calloc(IO_BUFFER, nfiles);
    EXPECT_TRUE(buffer);

    int error = 0;
    char filepath[2 * PATH_MAX]{0};
    // opening (truncating) all files
    for (int i = 0; i < nfiles; ++i) {
        sprintf(filepath, fmtin, dirname, i);
        fp[i] = fopen(filepath, "w");

        EXPECT_TRUE(fp[i]);
        EXPECT_EQ(setvbuf(fp[i], buffer[i], _IOFBF, IO_BUFFER), 0);
    }
    if (!error) {
        char *buffer = (char *) calloc(maxphraselen, 1);
        if (!buffer) {
            perror("malloc");
            error = -1;
        }
        if (!error) {
            size_t cnt = 0;
            for (ssize_t i = 0; i < nlines; ++i) {
                char *line = getrandomphrase(buffer, maxphraselen);
                size_t n   = strlen(line);
                if (fwrite(line, 1, n, fp[cnt]) != n) {
                    perror("fwrite");
                    error = -1;
                    break;
                }
                cnt = (cnt + 1) % nfiles; // generiting one line for each file in a rr fashion
            }
        }
    }

    // closing all files
    for (int i = 0; i < nfiles; ++i) {
        if (fp[i]) {
            fclose(fp[i]);
        }
    }
    free(fp);

    gettimeofday(&after, NULL);
    double elapsed_time = diffmsec(after, before);
    fprintf(stdout, "SPLIT elapsed time (ms) : %g\n", elapsed_time);

    return error;
}

TEST(integrationTests, RunSplitProcess) {

    EXPECT_EQ(splitFunction(10, 10, std::getenv("CAPIO_DIR")), 0);
}

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    // Destroy CAPIO SERVER and recreate it between each test execution
    // to provide a clean environment for each test
    testing::TestEventListeners &listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new CapioEnvironmentHandler(envp));

    return RUN_ALL_TESTS();
}

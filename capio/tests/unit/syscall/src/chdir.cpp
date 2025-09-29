#include <gtest/gtest.h>

#include <climits>
#include <sys/stat.h>
#include <unistd.h>

TEST(SystemCallTest, TestChdirOnExternalCapioDirAndThenBack) {

    char path[PATH_MAX];

    EXPECT_NE(getcwd(path, PATH_MAX), nullptr);
    EXPECT_EQ(chdir("/tmp"), 0);
    EXPECT_EQ(chdir(path), 0);
}
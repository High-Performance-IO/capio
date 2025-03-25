#include <gtest/gtest.h>

#include <filesystem>

#include <fcntl.h>

#include <capio/config.hpp>

#include "utils/filesystem.hpp"

class RealpathPosixTest : public testing::Test {
  protected:
    static void SetUpTestSuite() {
        capio_config = new CapioConfig();
        init_filesystem();
    }

    static void TearDownTestSuite() { destroy_filesystem(); }
};

TEST_F(RealpathPosixTest, TestAbsolutePathsInCapioDirWhenPathExists) {
    const std::filesystem::path PATHNAME = std::filesystem::path(capio_config->CAPIO_DIR) / "test";
    EXPECT_NE(mkdir(PATHNAME.c_str(), S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME.c_str(), F_OK), 0);
    EXPECT_EQ(capio_posix_realpath(PATHNAME), PATHNAME);
    EXPECT_NE(rmdir(PATHNAME.c_str()), -1);
    EXPECT_NE(access(PATHNAME.c_str(), F_OK), 0);
}

TEST_F(RealpathPosixTest, TestAbsolutePathsInCapioDirWhenPathDoesNotExist) {
    const std::filesystem::path PATHNAME = std::filesystem::path(capio_config->CAPIO_DIR) / "test";
    EXPECT_EQ(capio_posix_realpath(PATHNAME), PATHNAME);
}

TEST_F(RealpathPosixTest, TestAbsolutePathsOutsideCapioDirWhenPathExists) {
    const std::filesystem::path PATHNAME = "/tmp/test";
    EXPECT_NE(mkdir(PATHNAME.c_str(), S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME.c_str(), F_OK), 0);
    EXPECT_EQ(capio_posix_realpath(PATHNAME), PATHNAME);
    EXPECT_NE(rmdir(PATHNAME.c_str()), -1);
    EXPECT_NE(access(PATHNAME.c_str(), F_OK), 0);
}

TEST_F(RealpathPosixTest, TestAbsolutePathOutsideCapioDirWhenPathDoesNotExist) {
    const std::filesystem::path PATHNAME = "/tmp/test";
    EXPECT_EQ(capio_posix_realpath(PATHNAME), PATHNAME);
}

TEST_F(RealpathPosixTest, TestRelativePathsInCapioDirWhenCwdIsCapioDir) {
    const std::filesystem::path &capio_dir = std::filesystem::path(capio_config->CAPIO_DIR);
    const std::filesystem::path PATHNAME   = capio_dir / "test";
    set_current_dir(capio_dir);
    EXPECT_NE(mkdir(PATHNAME.c_str(), S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME.c_str(), F_OK), 0);
    EXPECT_EQ(capio_posix_realpath("test"), PATHNAME);
    EXPECT_NE(rmdir(PATHNAME.c_str()), -1);
    EXPECT_NE(access(PATHNAME.c_str(), F_OK), 0);
}

TEST_F(RealpathPosixTest, TestRelativePathsInCapioDirWhenCwdIsParentOfCapioDir) {
    const std::filesystem::path &capio_dir = std::filesystem::path(capio_config->CAPIO_DIR);
    const std::filesystem::path PATHNAME   = capio_dir / "test";
    set_current_dir(capio_dir.parent_path());
    EXPECT_NE(mkdir(PATHNAME.c_str(), S_IRWXU), -1);
    EXPECT_EQ(access(PATHNAME.c_str(), F_OK), 0);
    EXPECT_EQ(capio_posix_realpath(capio_dir.filename() / "test"), PATHNAME);
    EXPECT_NE(rmdir(PATHNAME.c_str()), -1);
    EXPECT_NE(access(PATHNAME.c_str(), F_OK), 0);
}
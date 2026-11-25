#ifndef CAPIO_TEST_UNIT_POSIX_PATH_HPP
#define CAPIO_TEST_UNIT_POSIX_PATH_HPP

#include <gtest/gtest.h>

#include "common/path.hpp"

TEST(PathTest, ForbiddenPrefixes) {
    EXPECT_TRUE(is_forbidden_path("/proc/self/status"));
    EXPECT_TRUE(is_forbidden_path("/sys/kernel"));
    EXPECT_FALSE(is_forbidden_path("/capio/proc/f"));
}

TEST(PathTest, TokensOfCapioClAreForbidden) {
    EXPECT_TRUE(is_forbidden_path("/capio/.f.commit"));
    EXPECT_TRUE(is_forbidden_path("/capio/d/.f.home_node"));
    EXPECT_TRUE(is_forbidden_path(".f.commit")); // a directory entry
}

TEST(PathTest, FilesThatOnlyLookLikeTokensAreNot) {
    EXPECT_FALSE(is_forbidden_path("/capio/f.commit"));    // not hidden
    EXPECT_FALSE(is_forbidden_path("/capio/.commit"));     // no name before the suffix
    EXPECT_FALSE(is_forbidden_path("/capio/.f.commit/g")); // inside a directory named so
    EXPECT_FALSE(is_forbidden_path("/capio/f"));
}

#endif // CAPIO_TEST_UNIT_POSIX_PATH_HPP

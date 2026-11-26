#include "harness.hpp"

#include <sys/xattr.h>

TEST_F(FsStreamingTest, AttributesComeFromTheKernelWithoutWaiting) {
    startServer({{"f"}});
    for (const auto &path : {root / "outside", work / "f"}) {
        std::ofstream(path) << "x";
        if (setxattr(path.c_str(), "user.note", "hello", 5, 0) != 0) {
            GTEST_SKIP() << "no user extended attributes in " << root;
        }
    }
    // f is an uncommitted input of cons: an attribute does not depend on its data
    EXPECT_OUTPUT(run("cons", {"xattr:get:../outside", "xattr:lget:../outside", "xattr:get:f",
                               "open:f", "xattr:fget", "xattr:flist"}),
                  "hellohellohellohellouser.note");
}

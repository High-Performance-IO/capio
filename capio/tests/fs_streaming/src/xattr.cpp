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
    EXPECT_OUTPUT(run("cons", {"getxattr:../outside", "lgetxattr:../outside", "getxattr:f",
                               "open:f", "fgetxattr", "flistxattr"}),
                  "hellohellohellohellouser.note");
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <filesystem>

#include <fcntl.h>

#include "utils/filesystem.hpp"

class FileSystemRegistrar : public Catch::EventListenerBase {
  public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(Catch::TestCaseInfo const &testCaseInfo) override { init_filesystem(); }

    void testCaseEnded(Catch::TestCaseStats const &testCaseStats) override { destroy_filesystem(); }
};

CATCH_REGISTER_LISTENER(FileSystemRegistrar);

TEST_CASE("Test absolute paths inside the CAPIO_DIR when path exists", "[posix]") {
    const std::string PATHNAME =
        std::filesystem::path(*get_capio_dir()) / std::filesystem::path("test");
    REQUIRE(mkdir(PATHNAME.c_str(), S_IRWXU) != -1);
    REQUIRE(access(PATHNAME.c_str(), F_OK) == 0);
    REQUIRE(*capio_posix_realpath(&PATHNAME) == PATHNAME);
    REQUIRE(rmdir(PATHNAME.c_str()) != -1);
    REQUIRE(access(PATHNAME.c_str(), F_OK) != 0);
}

TEST_CASE("Test absolute paths inside the CAPIO_DIR when path does not exist", "[posix]") {
    const std::string PATHNAME =
        std::filesystem::path(*get_capio_dir()) / std::filesystem::path("test");
    REQUIRE(*capio_posix_realpath(&PATHNAME) == PATHNAME);
}

TEST_CASE("Test absolute paths outside the CAPIO_DIR when path exists", "[posix]") {
    const std::string PATHNAME = "/tmp/test";
    REQUIRE(mkdir(PATHNAME.c_str(), S_IRWXU) != -1);
    REQUIRE(access(PATHNAME.c_str(), F_OK) == 0);
    REQUIRE(*capio_posix_realpath(&PATHNAME) == PATHNAME);
    REQUIRE(rmdir(PATHNAME.c_str()) != -1);
    REQUIRE(access(PATHNAME.c_str(), F_OK) != 0);
}

TEST_CASE("Test absolute paths outside the CAPIO_DIR when path does not exist", "[posix]") {
    const std::string PATHNAME = "/tmp/test";
    REQUIRE(*capio_posix_realpath(&PATHNAME) == PATHNAME);
}

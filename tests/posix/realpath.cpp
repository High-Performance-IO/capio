#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include "capio/filesystem.hpp"

TEST_CASE("Test capio_realpath", "[posix]") {
    symlink("/dev/zero", "./null");

    char *current_path = capio_realpath("./", nullptr);
    REQUIRE(strcmp(capio_realpath("./null", nullptr), "/dev/zero") == 0);
    char *resolved = nullptr;
    REQUIRE(strcmp(capio_realpath("/proc/self/cwd", resolved), current_path) == 0);

    unlink("./null");
}


TEST_CASE("Test capio_posix_realpath", "[posix]") {
    std::string capio_file = "./pluto.txt";
    std::string capio_dir = "pippo";
    std::string current_dir = "pippo";
    std::string abs_file = "/dev/zero";

    //test for capio file
    REQUIRE(strcmp(capio_posix_realpath(gettid(), &capio_file, &capio_dir, &current_dir)->c_str(), "pippo/pluto.txt") == 0);

    //test for absolute file that is not a cpaio file
    const char *resolved = capio_posix_realpath(gettid(), &abs_file, &capio_dir, &current_dir)->c_str();
    REQUIRE(strcmp(resolved, "/dev/zero") == 0);

}
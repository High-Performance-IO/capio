#include <gtest/gtest.h>

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// The entries of a directory, without "." and "..", with the type getdents gives each one.
static std::map<std::string, unsigned char> list_directory(const char *path) {
    std::map<std::string, unsigned char> entries;
    DIR *dir = opendir(path);
    EXPECT_NE(dir, nullptr);
    if (dir == nullptr) {
        return entries;
    }
    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            entries[name] = entry->d_type;
        }
    }
    EXPECT_EQ(closedir(dir), 0);
    return entries;
}

static void create_file(const char *path) {
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    EXPECT_NE(fd, -1);
    EXPECT_EQ(write(fd, "data", 4), 4);
    EXPECT_EQ(close(fd), 0);
}

TEST(SystemCallTest, TestDirectoryEntriesHaveTheirOwnType) {
    EXPECT_EQ(mkdir("listing_types", S_IRWXU), 0);
    EXPECT_EQ(mkdir("listing_types/sub", S_IRWXU), 0);
    create_file("listing_types/file");

    const auto entries = list_directory("listing_types");
    EXPECT_EQ(entries.size(), 2);
    EXPECT_EQ(entries.at("sub"), DT_DIR);
    EXPECT_EQ(entries.at("file"), DT_REG);

    EXPECT_EQ(unlink("listing_types/file"), 0);
    EXPECT_EQ(rmdir("listing_types/sub"), 0);
    EXPECT_EQ(rmdir("listing_types"), 0);
}

TEST(SystemCallTest, TestUnlinkAndRmdirRemoveTheEntry) {
    EXPECT_EQ(mkdir("listing_remove", S_IRWXU), 0);
    EXPECT_EQ(mkdir("listing_remove/sub", S_IRWXU), 0);
    create_file("listing_remove/file");
    create_file("listing_remove/kept");

    EXPECT_EQ(unlink("listing_remove/file"), 0);
    EXPECT_EQ(rmdir("listing_remove/sub"), 0);
    const auto entries = list_directory("listing_remove");
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.count("kept"), 1);

    EXPECT_EQ(unlink("listing_remove/kept"), 0);
    EXPECT_EQ(rmdir("listing_remove"), 0);
}

TEST(SystemCallTest, TestRenameMovesTheEntry) {
    EXPECT_EQ(mkdir("listing_from", S_IRWXU), 0);
    EXPECT_EQ(mkdir("listing_to", S_IRWXU), 0);
    create_file("listing_from/file");

    EXPECT_EQ(rename("listing_from/file", "listing_to/renamed"), 0);
    EXPECT_TRUE(list_directory("listing_from").empty());
    const auto entries = list_directory("listing_to");
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.count("renamed"), 1);

    EXPECT_EQ(unlink("listing_to/renamed"), 0);
    EXPECT_EQ(rmdir("listing_from"), 0);
    EXPECT_EQ(rmdir("listing_to"), 0);
}

TEST(SystemCallTest, TestRemoveAllOfANestedDirectory) {
    // Not create_directories: it stats each directory first, and a stat of a path no one has
    // produced yet waits for it to appear.
    EXPECT_EQ(mkdir("listing_tree", S_IRWXU), 0);
    EXPECT_EQ(mkdir("listing_tree/a", S_IRWXU), 0);
    EXPECT_EQ(mkdir("listing_tree/a/b", S_IRWXU), 0);
    create_file("listing_tree/a/b/file");
    create_file("listing_tree/a/file");
    create_file("listing_tree/file");

    EXPECT_EQ(std::filesystem::remove_all("listing_tree"), 6);
    EXPECT_NE(access("listing_tree", F_OK), 0);
}

TEST(SystemCallTest, TestChmodAndChownByPath) {
    EXPECT_EQ(mkdir("listing_mode", S_IRWXU), 0);
    create_file("listing_mode/file");

    EXPECT_EQ(chmod("listing_mode", S_IRWXU), 0);
    EXPECT_EQ(chmod("listing_mode/file", S_IRUSR | S_IWUSR), 0);
    EXPECT_EQ(fchmodat(AT_FDCWD, "listing_mode/file", S_IRUSR, 0), 0);
    EXPECT_EQ(chown("listing_mode/file", getuid(), getgid()), 0);
    EXPECT_EQ(fchownat(AT_FDCWD, "listing_mode", getuid(), getgid(), 0), 0);
    EXPECT_EQ(chmod("listing_mode/missing", S_IRWXU), -1);
    EXPECT_EQ(errno, ENOENT);

    EXPECT_EQ(unlink("listing_mode/file"), 0);
    EXPECT_EQ(rmdir("listing_mode"), 0);
}

TEST(SystemCallTest, TestDotNamesTheCapioDirectoryAfterChdir) {
    EXPECT_EQ(mkdir("listing_cwd", S_IRWXU), 0);
    create_file("listing_cwd/inside");
    const std::filesystem::path before = std::filesystem::current_path();

    // The kernel stays in the directory before: "." must still name the CAPIO one.
    EXPECT_EQ(chdir("listing_cwd"), 0);
    const auto entries = list_directory(".");
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.count("inside"), 1);
    EXPECT_EQ(chdir(before.c_str()), 0);

    EXPECT_EQ(unlink("listing_cwd/inside"), 0);
    EXPECT_EQ(rmdir("listing_cwd"), 0);
}

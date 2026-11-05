#include "common.hpp"

#include <fcntl.h>
#include <unistd.h>

// Runs before Merge, while the input map produced on its node has not been read here yet: the
// copy goes from the server of map to the server of merge, and never through this process.
TEST(multinodeIntegration, CopyFileRangeOfARemoteFile) {
    const char *directory = getenv("CAPIO_DIR");
    char path_in[4096], path_out[4096];
    snprintf(path_in, sizeof(path_in), "%s/outfile_%05d.dat", directory, 0);
    snprintf(path_out, sizeof(path_out), "%s/result_copy.dat", directory);

    const int in = open(path_in, O_RDONLY);
    ASSERT_GE(in, 0);
    const int out = open(path_out, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_GE(out, 0);
    ssize_t copied;
    size_t total = 0;
    while ((copied = copy_file_range(in, nullptr, out, nullptr, 1 << 20, 0)) > 0) {
        total += copied;
    }
    ASSERT_EQ(copied, 0) << strerror(errno);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), static_cast<off_t>(total));
    ASSERT_EQ(close(out), 0);
    ASSERT_EQ(close(in), 0);

    FILE *file = fopen(path_in, "r");
    ASSERT_NE(file, nullptr);
    const auto expected = read_data(file);
    fclose(file);
    file = fopen(path_out, "r");
    ASSERT_NE(file, nullptr);
    const auto actual = read_data(file);
    fclose(file);
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(total, expected.size());
    EXPECT_EQ(actual, expected);
}

TEST(multinodeIntegration, Merge) {
    const char *directory = getenv("CAPIO_DIR");
    std::vector<char> data;
    char path[4096];
    for (int i = 0; i < 2; ++i) {
        snprintf(path, sizeof(path), "%s/outfile_%05d.dat", directory, i);
        FILE *file = fopen(path, "r");
        ASSERT_NE(file, nullptr);
        auto chunk = read_data(file);
        fclose(file);
        data.insert(data.end(), chunk.begin(), chunk.end());
    }
    ASSERT_FALSE(data.empty());
    snprintf(path, sizeof(path), "%s/result.dat", directory);
    FILE *result = fopen(path, "w");
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(fwrite(data.data(), 1, data.size(), result), data.size());
    fclose(result);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }

#include "common.hpp"

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

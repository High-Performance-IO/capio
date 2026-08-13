#include "common.hpp"

TEST(multinodeIntegration, Split) {
    const char *directory = getenv("CAPIO_DIR");
    constexpr int file_count = 10;
    ASSERT_LE(file_count, max_files);

    std::vector<FILE *> files(file_count);
    char path[4096];
    for (int i = 0; i < file_count; ++i) {
        snprintf(path, sizeof(path), "%s/infile_%05d.dat", directory, i);
        ASSERT_NE(files[i] = fopen(path, "w"), nullptr);
    }
    for (int i = 0; i < file_count; ++i) {
        const char line[] = "CAPIO multinode map reduce input\n";
        ASSERT_EQ(fwrite(line, 1, sizeof(line) - 1, files[i]), sizeof(line) - 1);
        fclose(files[i]);
    }
}

int main(int argc, char **argv) { return run_tests(argc, argv); }

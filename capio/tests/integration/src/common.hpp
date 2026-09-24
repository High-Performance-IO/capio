#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

constexpr int max_phrase_length = 1024;
constexpr int max_files         = 10000;
constexpr size_t reduce_chunk   = 10240;

inline std::vector<char> read_data(FILE *file) {
    std::vector<char> data;
    char *line      = nullptr;
    size_t capacity = 0;
    ssize_t length;
    while (errno = 0, (length = getline(&line, &capacity, file)) > 0) {
        data.insert(data.end(), line, line + length);
    }
    EXPECT_EQ(errno, 0);
    free(line);
    return data;
}

inline int write_data(const std::vector<char> &data, float percent, const char *directory,
                      int first, int count) {
    std::vector<FILE *> files(count);
    char path[4096];
    for (int i = 0; i < count; ++i) {
        snprintf(path, sizeof(path), "%s/outfile_%05d.dat", directory, first + i);
        files[i] = fopen(path, "w");
        if (!files[i]) {
            return -1;
        }
    }

    size_t remaining = data.size() * percent;
    size_t offset    = 0;
    for (size_t i = 0; remaining; ++i) {
        const size_t size = std::min(remaining, reduce_chunk);
        if (fwrite(data.data() + offset, 1, size, files[i % files.size()]) != size) {
            return -1;
        }
        offset += size;
        remaining -= size;
    }
    for (FILE *file : files) {
        fclose(file);
    }
    return 0;
}

inline int run_tests(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

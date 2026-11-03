#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "remote/backend/none.hpp"
#include "utils/location.hpp"

extern Backend *backend;

// Requests and the threads that wait for a file read the location files at the same time. A seek
// and the reads after it must not interleave with another thread's, or a lookup misses a path
// that is there, or fseek fails and the server exits.
TEST(LocationTest, ConcurrentLookupsFindEveryWrittenPath) {
    const auto previous_directory = std::filesystem::current_path();
    const auto directory = std::filesystem::temp_directory_path() / "capio-location-test";
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);

    char program[] = "capio_server";
    char *argv[]   = {program, nullptr};
    Backend *previous_backend = backend;
    backend                   = new NoneBackend(1, argv);
    // Earlier tests opened a location file of the same name elsewhere: set it aside, and use one here.
    const auto previous_fps = files_location_fps;
    FILE *previous_fp       = files_location_fp;
    const int previous_fd   = files_location_fd;
    files_location_fps.clear();
    open_files_location();

    constexpr int PATHS = 200;
    for (int i = 0; i < PATHS; i++) {
        write_file_location("/capio/file" + std::to_string(i));
    }

    std::atomic<int> misses{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; t++) {
        readers.emplace_back([&misses, t] {
            for (int round = 0; round < 50; round++) {
                for (int i = t; i < PATHS; i += 8) {
                    const std::filesystem::path path = "/capio/file" + std::to_string(i);
                    erase_from_files_location(path);
                    if (!load_file_location(path)) {
                        misses++;
                    }
                }
            }
        });
    }
    std::thread writer([] {
        for (int i = 0; i < 500; i++) {
            write_file_location("/capio/late" + std::to_string(i));
        }
    });
    for (auto &reader : readers) {
        reader.join();
    }
    writer.join();

    EXPECT_EQ(misses, 0);

    fclose(files_location_fp);
    files_location_fps = previous_fps;
    files_location_fp  = previous_fp;
    files_location_fd  = previous_fd;
    delete backend;
    backend = previous_backend;
    std::filesystem::current_path(previous_directory);
    std::filesystem::remove_all(directory);
}

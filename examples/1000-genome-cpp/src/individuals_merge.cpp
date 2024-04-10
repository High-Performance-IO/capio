#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

void print_usage() {
    std::cerr << "individuals_merge CHROMOSOME INPUT_FOLDER..." << std::endl;
}

int main(int argc, char *argv[]) {
    // Start timer
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // Retrieve input arguments
    const int chr = std::stoi(argv[1]);
    auto folders = new std::string[argc - 2];
    for (int i = 2; i < argc; i++) {
        folders[i - 2] = argv[i];
    }

    // Create merged directory
    std::ostringstream out_name;
    out_name << "chr" << chr << "n";
    std::cout << "Creating merged folder " << out_name.str() << "..." << std::endl;
    std::filesystem::path merged_path(out_name.str());
    std::filesystem::create_directories(merged_path);

    // For each folder
    for (int i = 0; i < argc - 2; i++) {
        const std::string &folder = folders[i];
        std::cout << "Merging " << folder << "..." << std::endl;
        for (auto const& dir_entry : std::filesystem::directory_iterator{folder}) {
            std::ifstream in_stream(dir_entry.path());
            std::ofstream out_stream(merged_path / std::filesystem::path(dir_entry.path()).filename(),
                                   std::ios::app);
            out_stream << in_stream.rdbuf();
        }
    }

    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    delete[] folders;

    // Return
    return 0;
}


#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <boost/algorithm/string.hpp>

void print_usage() {
    std::cerr << "individuals_merge CHROMOSOME INPUT_FOLDER..." << std::endl;
}

std::vector<std::string> split(const std::string &str, const std::string &chars) {
    std::vector<std::string> tokens;
    boost::split(tokens, str, boost::is_any_of(chars));
    return tokens;
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

    // Read column file
    const std::filesystem::path col_file("data/20130502/columns.txt");
    std::ifstream col_stream(col_file);
    if (!col_stream) {
        std::cerr << "Error reading file " << col_file << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string col_names;
    std::getline(col_stream, col_names);
    boost::algorithm::trim_right(col_names);
    std::vector<std::string> column_data = split(col_names, "\t");
    column_data.erase(column_data.begin(), column_data.begin() + 9);

    // Create merged directory
    std::ostringstream out_name;
    out_name << "chr" << chr << "n";
    std::cout << "Creating merged folder " << out_name.str() << "..." << std::endl;
    std::filesystem::path merged_path(out_name.str());
    std::filesystem::create_directories(merged_path);

    // For each column
    for (const auto & col : column_data) {
        std::ostringstream file_name;
        file_name << "chr" << chr << "." << col;
        std::cout << "Merging " << file_name.str() << "..." << std::endl;
        for (int i = 0; i < argc - 2; i++) {
            const std::string &folder = folders[i];
            std::ifstream in_stream(std::filesystem::path(folder) / std::filesystem::path(file_name.str()));
            std::ofstream out_stream(merged_path / std::filesystem::path(file_name.str()), std::ios::app);
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


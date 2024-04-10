#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>


void print_usage() {
    std::cerr << "individuals INPUT_FILE CHROMOSOME COUNTER STOP TOTAL" << std::endl;
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
    if (argc != 6) {
        print_usage();
        exit(EXIT_FAILURE);
    }
    std::filesystem::path in_path(argv[1]);
    const int chr = std::stoi(argv[2]);
    const int counter = std::stoi(argv[3]);
    const int stop = std::stoi(argv[4]);
    const int total = std::stoi(argv[5]);
    const int ending = std::min(stop, total);

    // Begin processing chromosome
    std::cout << "Processing chromosome " << chr << std::endl;

    // Read file chunk
    std::ifstream in_stream(in_path);
    if (!in_stream) {
        std::cerr << "Error reading file " << in_path << std::endl;
        exit(EXIT_FAILURE);
    }
    std::vector<std::string> raw_data;
    for (int i = 0; i < ending; i++) {
        std::string line;
        std::getline(in_stream, line);
        if (i >= (counter - 1)) {
            raw_data.push_back(line);
        }
    }
    in_stream.close();

    // Discard comments
    std::cout << "Total number of raw_data: " << total << std::endl;
    std::cout << "Processing " << in_path << " from line " << counter << " to line " << ending << std::endl;
    const std::regex no_comments("^#.*$");
    std::vector<std::string> data;
    std::copy_if(
            raw_data.begin(),
            raw_data.end(),
            std::back_inserter(data),
            [&no_comments](std::string &line) {
                return !(std::regex_match(line, no_comments));
            });

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
    std::cout << "Number of columns: " << column_data.size() << std::endl;

    // For each line
    auto chunks = new std::string *[data.size()];
    auto left_col_values = new int *[data.size()];
    for (int i = 0; i < data.size(); i++) {

        // Split in tokens
        const std::string &line = data[i];
        const std::vector<std::string> line_data = split(line, "\t");
        chunks[i] = new std::string[5];
        std::copy(line_data.begin() + 1, line_data.begin() + 5, chunks[i]);
        chunks[i][4] = split(split(line_data[7], ";")[8], "=")[1];

        // Concatenate column part with rows data
        left_col_values[i] = new int[column_data.size()];
        for (int j = 0; j < column_data.size(); j++) {
            left_col_values[i][j] = stoi(split(line_data[j + 9], "|")[0]);
        }
    }

    // Create output directory
    std::ostringstream out_name;
    out_name << "chr" << chr << "n-" << counter << "-" << ending;
    std::filesystem::path out_path(out_name.str());
    std::filesystem::create_directories(out_path);

    // For each column
    for (int col = 0; col < column_data.size(); col++) {

        // Write chunk file
        std::string &name = column_data[col];
        std::ostringstream chunk_file_name;
        chunk_file_name << "chr" << chr << "." << name;
        const std::filesystem::path chunk_file_path = out_path / std::filesystem::path(chunk_file_name.str());
        std::cout << "Writing file " << chunk_file_path << std::endl;
        std::ofstream chunk_file(chunk_file_path);
        std::ostringstream null_values_buffer;
        for (int j = 0; j < data.size(); j++) {
            float af_value;
            try {
                if (chunks[j][4].find(',') != std::string::npos) {
                    af_value = stof(split(chunks[j][4], ",")[0]);
                } else {
                    af_value = stof(chunks[j][4]);
                }
            } catch (std::invalid_argument &e) {
                af_value = std::numeric_limits<float>::max();
            }
            const int left_col_value = left_col_values[j][col];
            if (af_value < 0.5 && left_col_value == 1) {
                null_values_buffer << chunks[j][0] << "        " << chunks[j][1] << "    "
                                   << chunks[j][2] << "    " << chunks[j][3] << "    " << chunks[j][4] << "\n";
            } else if (af_value >= 0.5 && left_col_value == 0) {
                chunk_file << chunks[j][0] << "        " << chunks[j][1] << "    "
                           << chunks[j][2] << "    " << chunks[j][3] << "    " << chunks[j][4] << "\n";
            }
        }
        chunk_file << null_values_buffer.str();
    }

    for (std::size_t i = 0; i < data.size(); ++i)
        delete[] chunks[i];
    delete[] chunks;
    delete[] left_col_values;
    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    // Return
    return 0;
}
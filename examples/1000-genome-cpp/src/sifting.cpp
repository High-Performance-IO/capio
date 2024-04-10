#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

#include <boost/algorithm/string.hpp>


void print_usage() {
    std::cerr << "sifting INPUT_FILE CHROMOSOME" << std::endl;
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
    if (argc != 3) {
        print_usage();
        exit(EXIT_FAILURE);
    }
    std::filesystem::path in_path(argv[1]);
    const int chr = std::stoi(argv[2]);

    // Count header lines
    std::ifstream in_stream(in_path);
    if (!in_stream) {
        std::cerr << "Error reading file " << in_path << std::endl;
        exit(EXIT_FAILURE);
    }
    int header = 0;
    for (int i = 0; i < 1000; i++) {
        std::string line;
        std::getline(in_stream, line);
        if (line.find('#') != std::string::npos) {
            header++;
        }
    }
    std::cout << header << std::endl;

    // Extract data and write them to the output file
    std::cout << "Taking columns from " << in_path << std::endl;
    std::ostringstream out_path;
    out_path << "sifted.SIFT.chr" << chr << ".txt";
    std::ofstream out_file(std::filesystem::path(out_path.str()));
    in_stream.seekg(0, std::ios::beg);
    std::string line;
    int counter = 0;
    std::regex content_in_parentheses("\\((.+?)\\)");
    std::smatch regex_result;
    while (std::getline(in_stream, line)) {
        counter++;
        if ((line.find("deleterious") != std::string::npos || line.find("tolerated") != std::string::npos) &&
            line.find("rs") != std::string::npos) {

            // Get data from tokens
            std::vector<std::string> line_data = split(line, "\t");
            std::vector<std::string> info_data = split(line_data[7], "|");
            out_file << (counter - header) << ' ' << line_data[2];
            out_file << ' ' << info_data[4];
            if (info_data.size() >= 17) {
                std::regex_search(info_data[16], regex_result, content_in_parentheses);
                out_file << ' ' << regex_result.str(1);
                if (info_data.size() >= 18) {
                    std::regex_search(info_data[17], regex_result, content_in_parentheses);
                    out_file << ' ' << regex_result.str(1);
                }
            }
            out_file << '\n';
        }
    }
    std::cout << "Line, id, ENGS id, SIFT and phenotype printed to " << out_path.str() << std::endl;

    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    // Return
    return 0;
}
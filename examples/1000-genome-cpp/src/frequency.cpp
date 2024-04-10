#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include "utils.hpp"


std::vector<std::string> intersect(const std::vector<std::string> &v1, const std::vector<std::string> &v2) {
    std::vector<std::string> v3;
    const std::vector<std::string> &shortest_vector = v1.size() < v2.size() ? v1 : v2;
    std::unordered_set<std::string> hash_table(shortest_vector.begin(), shortest_vector.end());
    for (const std::string &elem: (shortest_vector == v1 ? v2 : v1)) {
        if (hash_table.find(elem) != hash_table.end()) {
            v3.push_back(elem);
        }
    }
    return v3;
}


std::vector<std::string> read_from_file(const std::filesystem::path &file_path) {
    std::ifstream file_stream(file_path);
    if (!file_stream) {
        std::cerr << "Error reading file " << file_path << std::endl;
        exit(EXIT_FAILURE);
    }
    return {std::istream_iterator<std::string>{file_stream}, std::istream_iterator<std::string>{}};
}


std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> tokens;
    boost::split(tokens, str, boost::algorithm::is_space());
    return tokens;
}


template<typename T>
std::string vec_to_string(std::vector<T> vec) {
    if (vec.empty()) {
        return "";
    }
    std::ostringstream stream;
    stream << "['" << vec[0];
    for (size_t i = 1; i < vec.size(); i++) {
        stream << "', '" << vec[i];
    }
    stream << "']";
    return stream.str();
}


int main(int argc, char *argv[]) {
    // Start timer
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point local_start_time, local_end_time;

    // Create argument parser
    int chr;
    std::string pop;
    boost::program_options::options_description parser("Process mutation sets (-c and -pop are required)");
    parser.add_options()
            ("c", boost::program_options::value(&chr)->required(), "type a chromosome 1-22")
            ("pop", boost::program_options::value(&pop)->required(),
             "type a population 0-6; 0:ALL, 1:EUR, 2:EAS, 3:AFR, 4:AMR, 5:SAS, 6:GBR");
    auto style = boost::program_options::command_line_style::style_t(
            boost::program_options::command_line_style::default_style |
            boost::program_options::command_line_style::allow_long_disguise);

    // Retrieve input arguments
    boost::program_options::variables_map variables_map;
    try {
        boost::program_options::store(
                boost::program_options::parse_command_line(argc, argv, parser, style),
                variables_map);
        boost::program_options::notify(variables_map);
    } catch (const std::exception &e) {
        std::cerr << parser << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables
    constexpr unsigned int n_runs = 1000;
    constexpr unsigned int n_indiv = 52;
    std::ostringstream sift_path_stream;
    sift_path_stream << "sifted.SIFT.chr" << chr << ".txt";
    std::filesystem::path sift_path(sift_path_stream.str());
    std::ostringstream freq_dir_stream;
    freq_dir_stream << "chr" << chr << '-' << pop << "-freq";
    std::filesystem::path freq_dir(freq_dir_stream.str());
    std::filesystem::path outdata_dir = freq_dir / std::filesystem::path("output_no_sift");
    std::filesystem::create_directories(outdata_dir);
    std::filesystem::path plot_dir = freq_dir / std::filesystem::path("plots_no_sift");
    std::filesystem::create_directories(plot_dir);
    std::ostringstream dest_path;
    dest_path << "chr" << chr << "n";

    // Read individuals
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Reading individuals" << std::endl;
    std::vector<std::string> ids = intersect(
            read_from_file(std::filesystem::path("data/populations") /
                           std::filesystem::path(pop)),
            read_from_file(std::filesystem::path("data/20130502") /
                           std::filesystem::path("columns.txt")));
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Read variations
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Reading in rs with sift scores below NO-SIFT" << std::endl;
    std::vector<std::string> rs_numbers;
    std::map<const std::string, const std::string> map_variations;
    std::ifstream sift_file(sift_path);
    if (!sift_file) {
        std::cerr << "Error reading file " << sift_path << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string sift_line;
    while (std::getline(sift_file, sift_line)) {
        std::vector<std::string> line_data = split(sift_line);
        if (line_data.size() > 2) {
            rs_numbers.push_back(line_data[1]);
            map_variations.insert({line_data[1], line_data[2]});
        }
    }
    sift_file.close();
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Read individual mutations
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Reading in individual mutation files" << std::endl;
    std::vector<std::vector<std::string>> mutation_index_array;
    mutation_index_array.reserve(ids.size());
    for (auto const &name: ids) {
        std::ostringstream file_name;
        file_name << "chr" << chr << '.' << name;
        std::filesystem::path file_path =
                std::filesystem::path(dest_path.str()) / std::filesystem::path(file_name.str());
        mutation_index_array.push_back(intersect(rs_numbers, read_from_file(file_path)));
        std::sort(mutation_index_array.back().begin(), mutation_index_array.back().end());
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write map variations
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream map_variations_filename;
    map_variations_filename << "map_variations" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path map_variations_path =
            outdata_dir / std::filesystem::path(map_variations_filename.str());
    std::cout << "Writing map_variations to " << map_variations_path << std::endl;
    std::ofstream map_variations_file(map_variations_path);
    for (auto const &[key, count]: map_variations) {
        map_variations_file << key << '\t' << count << '\n';
    }
    map_variations_file.close();
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write mutation index array
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream mutation_index_array_filename;
    mutation_index_array_filename << "mutation_index_array" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path mutation_index_array_path =
            outdata_dir / std::filesystem::path(mutation_index_array_filename.str());
    std::cout << "Writing mutation array to " << mutation_index_array_path << std::endl;
    std::ofstream mutation_index_array_file(mutation_index_array_path);
    for (const std::vector<std::string> &sifted_mutation: mutation_index_array) {
        mutation_index_array_file << vec_to_string(sifted_mutation) << '\n';
    }
    mutation_index_array_file.close();
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Calculating the number of overlapings mutations
    local_start_time = std::chrono::steady_clock::now();
    size_t n_p = mutation_index_array.size();
    std::cout << "Calculating the number of overlapings mutations between "
              << n_indiv << " individuals selected randomly" << std::endl;
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> distribution(0, n_p - 1);
    std::array<std::map<const std::string, unsigned int>, n_runs> mutation_overlap;
    std::array<std::vector<std::string>, n_runs> random_individuals;
    for (unsigned int run = 0; run < n_runs; run++) {
        auto random_list = new size_t[n_p];
        for (int i = 0; i < n_p; i++) {
            random_list[i] = distribution(rng);
        }
        for (unsigned int pq = 0; pq < n_indiv; pq++) {
            if (2 * pq >= n_p) {
                break;
            }
            for (std::string &index: mutation_index_array[random_list[2 * pq]]) {
                if (mutation_overlap[run].find(index) != mutation_overlap[run].end()) {
                    mutation_overlap[run][index]++;
                } else {
                    mutation_overlap[run][index] = 1;
                }
            }
            //std::cout << "time, individual: " << ids[random_list[2 * pq]] << std::endl;
            random_individuals[run].push_back(ids[random_list[2 * pq]]);
        }
        delete[] random_list;
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Calculating the histogram of overlapping mutations
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Calculating the frequency/historgram of overlapings mutations" << std::endl;
    std::array<std::map<const unsigned int, unsigned int>, n_runs> histogram_overlap;
    for (unsigned int run = 0; run < n_runs; run++) {
        for (auto const &[item, count]: mutation_overlap[run]) {
            if (histogram_overlap[run].find(count) != histogram_overlap[run].end()) {
                histogram_overlap[run][count]++;
            } else {
                histogram_overlap[run][count] = 1;
            }
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write mutation overlap
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream mutation_overlap_basename;
    mutation_overlap_basename << "Mutation_overlap_chr" << chr << "_sNO-SIFT_" << pop << "_";
    std::cout << "Writing mutations overlapping to " << mutation_overlap_basename.str() << std::endl;
    for (unsigned int run = 0; run < n_runs; run++) {
        std::ostringstream mutation_overlap_filename;
        mutation_overlap_filename << mutation_overlap_basename.str() << run << ".txt";
        std::filesystem::path mutation_overlap_path =
                outdata_dir / std::filesystem::path(mutation_overlap_filename.str());
        std::ofstream mutation_overlap_file(mutation_overlap_path);
        mutation_overlap_file << "Mutation Index- Number Overlapings \n";
        for (auto const &[key, count]: mutation_overlap[run]) {
            mutation_overlap_file << key << '-' << count << '\n';
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write histogram overlap
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream histogram_overlap_basename;
    histogram_overlap_basename << "Histogram_mutation_overlap_chr" << chr << "_sNO-SIFT_" << pop << "_";
    std::cout << "Writing frequency histogram of mutations overlapping to "
              << histogram_overlap_basename.str() << std::endl;
    for (unsigned int run = 0; run < n_runs; run++) {
        std::ostringstream histogram_overlap_filename;
        histogram_overlap_filename << histogram_overlap_basename.str() << run << ".txt";
        std::filesystem::path histogram_overlap_path =
                outdata_dir / std::filesystem::path(histogram_overlap_filename.str());
        std::ofstream histogram_overlap_file(histogram_overlap_path);
        histogram_overlap_file << "Number Individuals - Number Mutations  \n";
        for (unsigned int i = 1; i <= n_indiv; i++) {
            if (histogram_overlap[run].find(i) != histogram_overlap[run].end()) {
                histogram_overlap_file << i << '-' << histogram_overlap[run][i] << '\n';
            } else {
                histogram_overlap_file << i << '-' << 0 << '\n';
            }
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write random individuals
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream random_indiv_basename;
    random_indiv_basename << "random_indiv" << chr << "_sNO-SIFT_" << pop << "_";
    std::cout << "Writing Random individuals to "
              << random_indiv_basename.str() << std::endl;
    for (unsigned int run = 0; run < n_runs; run++) {
        std::ostringstream random_indiv_filename;
        random_indiv_filename << random_indiv_basename.str() << run << ".txt";
        std::filesystem::path random_indiv_path =
                outdata_dir / std::filesystem::path(random_indiv_filename.str());
        std::ofstream random_indiv_file(random_indiv_path);
        random_indiv_file << "Individuals \n";
        for (const std::string& item : random_individuals[run]) {
            random_indiv_file << item << '\n';
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Generate final output
    std::ostringstream output_name;
    output_name << "chr" << chr << '-' << pop << "-freq.tar.gz";
    std::filesystem::path out_path(output_name.str());
    std::filesystem::path workdir = std::filesystem::current_path();
    std::filesystem::current_path(freq_dir);
    create_tar_archive(
            std::vector{outdata_dir.filename(), plot_dir.filename()},
            std::filesystem::path(out_path));
    std::filesystem::current_path(workdir);
    std::filesystem::rename(freq_dir / out_path, std::filesystem::current_path() / out_path);

    // Remove temporary directories
    std::filesystem::remove_all(freq_dir);

    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    // Return
    return 0;
}

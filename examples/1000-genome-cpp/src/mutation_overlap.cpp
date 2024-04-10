#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include "combinations.h"
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


std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> tokens;
    boost::split(tokens, str, boost::algorithm::is_space());
    return tokens;
}


std::vector<std::string> split(const std::string &str, const std::string &chars) {
    std::vector<std::string> tokens;
    boost::split(tokens, str, boost::is_any_of(chars));
    return tokens;
}


std::vector<std::string> read_from_file(const std::filesystem::path &file_path) {
    std::ifstream file_stream(file_path);
    if (!file_stream) {
        std::cerr << "Error reading file " << file_path << std::endl;
        exit(EXIT_FAILURE);
    }
    return {std::istream_iterator<std::string>{file_stream}, std::istream_iterator<std::string>{}};
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


void write_pair_individuals(const std::filesystem::path &out_path, size_t **data, size_t x_len, size_t y_len) {
    std::cout << "Writing pairs overlapping mutations to " << out_path << std::endl;
    std::ofstream out_file(out_path);
    for (int i = 0; i < x_len; i++) {
        for (int j = 0; j < y_len; j++) {
            if (j > 0) {
                out_file << " ";
            }
            out_file << data[i][j];
        }
        out_file << '\n';
    }
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
    constexpr unsigned int n_runs = 1;
    std::ostringstream sift_path_stream;
    sift_path_stream << "sifted.SIFT.chr" << chr << ".txt";
    std::filesystem::path sift_path(sift_path_stream.str());
    std::filesystem::path outdata_dir = std::filesystem::path("output_no_sift");
    std::filesystem::create_directories(outdata_dir);
    std::filesystem::path plot_dir = std::filesystem::path("plots_no_sift");
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
    std::unordered_map<std::string, const size_t> total_mutations;
    total_mutations.reserve(ids.size());
    std::vector<size_t> total_mutations_list;
    total_mutations_list.reserve(ids.size());
    for (auto const &name: ids) {
        std::ostringstream file_name;
        file_name << "chr" << chr << '.' << name;
        std::filesystem::path file_path =
                std::filesystem::path(dest_path.str()) / std::filesystem::path(file_name.str());
        mutation_index_array.push_back(intersect(rs_numbers, read_from_file(file_path)));
        std::sort(mutation_index_array.back().begin(), mutation_index_array.back().end());
        total_mutations.insert({name, mutation_index_array.back().size()});
        total_mutations_list.push_back(mutation_index_array.back().size());
    }
    std::cout << "Mutation index array for " << ids[0] << " : " << vec_to_string(mutation_index_array[0]) << std::endl;
    std::cout << "total_len_mutations for " << ids[0] << " : " << total_mutations.at(ids[0]) << std::endl;
    std::cout << "total_mutations_list is " << vec_to_string(total_mutations_list) << std::endl;
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write total individuals
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream total_mutations_filename;
    total_mutations_filename << "total_mutations_individual" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path total_mutations_path =
            outdata_dir / std::filesystem::path(total_mutations_filename.str());
    std::cout << "Writing total mutations list per individual to " << total_mutations_path << std::endl;
    std::ofstream total_mutations_file(total_mutations_path);
    for (auto const &name: ids) {
        total_mutations_file << name << '\t' << total_mutations[name] << '\n';
    }
    total_mutations_file.close();
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

    // Extract half pair individuals
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Cross matching mutations in individuals - half with half" << std::endl;
    size_t n_p = mutation_index_array.size();
    size_t n_pairs = std::ceil(static_cast<float>(n_p) / 2);
    auto half_pairs_overlap = new size_t *[n_pairs];
    for (size_t run = 0; run < n_pairs; run++) {
        half_pairs_overlap[run] = new size_t[n_pairs]{};
        for (size_t pq = n_pairs + 1; pq < n_p; pq++) {
            half_pairs_overlap[run][pq - n_pairs - 1] =
                    intersect(mutation_index_array[run], mutation_index_array[pq]).size();
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Extract total pair individuals
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Cross matching mutations total individuals" << std::endl;
    auto total_pairs_overlap = new size_t *[n_p];
    auto simmetric_overlap = new size_t *[n_p];
    for (size_t run = 0; run < n_p; run++) {
        total_pairs_overlap[run] = new size_t[n_p]{};
        simmetric_overlap[run] = new size_t[n_p]{};
    }
    for (size_t run = 0; run < n_p; run++) {
        for (size_t pq = run + 1; pq < n_p; pq++) {
            total_pairs_overlap[run][pq] =
                    intersect(mutation_index_array[run], mutation_index_array[pq]).size();
            simmetric_overlap[run][pq] = simmetric_overlap[pq][run] = total_pairs_overlap[run][pq];
        }
    }
    delete[] simmetric_overlap;
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;


    // Extract pair individuals
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Cross matching mutations in individuals" << std::endl;
    auto random_pairs_overlap = new size_t *[n_runs];
    std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> distribution(0, n_p - 1);
    for (unsigned int run = 0; run < n_runs; run++) {
        auto random_list = new size_t[n_p];
        for (int i = 0; i < n_p; i++) {
            random_list[i] = distribution(rng);
        }
        random_pairs_overlap[run] = new size_t[n_pairs];
        for (int pq = 0; pq < n_pairs; pq++) {
            random_pairs_overlap[run][pq] = intersect(
                    mutation_index_array[random_list[2 * pq]],
                    mutation_index_array[random_list[2 * pq]]).size();
        }
        delete[] random_list;
    }
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

    // Write pair individuals
    std::ostringstream half_individuals_filename;
    half_individuals_filename << "individual_half_pairs_overlap_chr" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path half_individuals_path =
            outdata_dir / std::filesystem::path(half_individuals_filename.str());
    write_pair_individuals(half_individuals_path, half_pairs_overlap, n_pairs, n_pairs);
    std::ostringstream total_individuals_filename;
    total_individuals_filename << "total_individual_pairs_overlap_chr" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path total_individuals_path =
            outdata_dir / std::filesystem::path(total_individuals_filename.str());
    write_pair_individuals(total_individuals_path, total_pairs_overlap, n_p, n_p);
    std::ostringstream random_individuals_filename;
    random_individuals_filename << "100_individual_overlap_chr" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::filesystem::path random_individuals_path =
            outdata_dir / std::filesystem::path(random_individuals_filename.str());
    write_pair_individuals(random_individuals_path, random_pairs_overlap, n_runs, n_pairs);

    //TODO: plot

    delete[] half_pairs_overlap;
    delete[] total_pairs_overlap;
    delete[] random_pairs_overlap;

    // Sample individual groups
    local_start_time = std::chrono::steady_clock::now();
    constexpr size_t n_groups = 26;
    std::vector<size_t> random_mutations_list;
    random_mutations_list.reserve(n_groups);
    for (unsigned int run = 0; run < n_runs; run++) {
        std::sample(total_mutations_list.begin(), total_mutations_list.end(),
                    std::back_inserter(random_mutations_list), n_groups, rng);
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write random mutations list
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream random_mutations_basename;
    random_mutations_basename << "random_mutations_individual" << chr << "_sNO-SIFT_" << pop;
    std::cout << "Writing a list of " << n_groups << " random individuals with the number mutations per indiv "
              << random_mutations_basename.str() << std::endl;
    for (unsigned int run = 0; run < n_runs; run++) {
        std::ostringstream random_mutations_filename;
        random_mutations_filename << random_mutations_basename.str() << "_run_" << run << ".txt";
        std::filesystem::path random_mutations_path =
                outdata_dir / std::filesystem::path(random_mutations_filename.str());
        std::ofstream random_mutations_file(random_mutations_path);
        for (const size_t &line: random_mutations_list) {
            random_mutations_file << line << '\n';
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Match variation pairs
    local_start_time = std::chrono::steady_clock::now();
    std::cout << "Cross matching pairs of variations" << std::endl;
    std::map<const std::string, unsigned int> gene_pair_list;
    for (int pp = 0; pp < n_p; pp++) {
        if (mutation_index_array[pp].size() >= 2) {
            for_each_combination(
                    mutation_index_array[pp].begin(),
                    mutation_index_array[pp].begin() + 2,
                    mutation_index_array[pp].end(),
                    [&gene_pair_list](std::vector<std::string>::iterator begin,
                                      std::vector<std::string>::iterator end) {
                        std::ostringstream key;
                        key << "('" << *begin << "', '" << *(end - 1) << "')";
                        if (gene_pair_list.find(key.str()) != gene_pair_list.end()) {
                            gene_pair_list[key.str()]++;
                        } else {
                            gene_pair_list[key.str()] = 1;
                        }
                        return false;
                    });
        }
    }
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Write gene pairs file
    local_start_time = std::chrono::steady_clock::now();
    std::ostringstream gene_pairs_filename;
    gene_pairs_filename << "gene_pairs_count_chr" << chr << "_sNO-SIFT_" << pop << ".txt";
    std::cout << "Writing gene pair list to " << gene_pairs_filename.str() << std::endl;
    std::filesystem::path gene_pairs_path =
            outdata_dir / std::filesystem::path(gene_pairs_filename.str());
    std::ofstream gene_pairs_file(gene_pairs_path);
    for (auto const& [key, count] : gene_pair_list) {
        gene_pairs_file << key << '\t' << count << '\n';
    }
    gene_pairs_file.close();
    local_end_time = std::chrono::steady_clock::now();
    std::cout << "time: "
              << std::chrono::duration_cast<std::chrono::seconds>(local_end_time - local_start_time).count()
              << " seconds" << std::endl;

    // Generate final output
    std::ostringstream output_name;
    output_name << "chr" << chr << '-' << pop << ".tar.gz";
    create_tar_archive(
            std::vector{outdata_dir, plot_dir},
            std::filesystem::path(output_name.str()));
    std::filesystem::path workdir = std::filesystem::current_path();

    // Remove temporary directories
    std::filesystem::remove_all(outdata_dir);
    std::filesystem::remove_all(plot_dir);

    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    // Return
    return 0;
}
#ifndef INC_1000_GENOME_UTILS_HPP
#define INC_1000_GENOME_UTILS_HPP

#include <array>
#include <filesystem>
#include <vector>


void create_tar_archive(const std::filesystem::path &in_path, const std::filesystem::path &out_path);
void create_tar_archive(const std::vector<std::filesystem::path> &in_paths, const std::filesystem::path &out_path);
void extract_tar_archive(const std::filesystem::path &in_path, const std::filesystem::path &out_path);

#endif //INC_1000_GENOME_UTILS_HPP

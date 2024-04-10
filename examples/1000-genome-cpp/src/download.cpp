#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <curl/curl.h>


void download_data(CURL *curl, const std::string &url, const std::filesystem::path &out_path) {
    std::cout << "Downloading " << url << " on " << out_path << std::endl;
    FILE *fp = fopen(out_path.c_str(), "wb");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Error downloading file " << out_path << ": " << curl_easy_strerror(res) << std::endl;
        exit(EXIT_FAILURE);
    }
    fclose(fp);
}


void decompress_data(const std::filesystem::path &in_path, const std::filesystem::path &out_path) {
    std::cout << "Decompressing " << in_path << std::endl;
    std::ifstream in_file(in_path, std::ios::in | std::ios::binary);
    if (!in_file) {
        std::cerr << "Error reading file " << in_path << std::endl;
        exit(EXIT_FAILURE);
    }
    std::ofstream out_file(out_path, std::ios::out | std::ios::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf;
    inbuf.push(boost::iostreams::gzip_decompressor());
    inbuf.push(in_file);
    std::istream in_stream(&inbuf);
    out_file << in_stream.rdbuf();
    in_file.close();
    std::filesystem::remove(in_path);
}


void print_usage() {
    std::cerr << "download CHROMOSOMES" << std::endl;
}


int main(int argc, char *argv[]) {
    // Start timer
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // Retrieve input arguments
    if (argc != 2) {
        print_usage();
        exit(EXIT_FAILURE);
    }
    const int chrs = std::stoi(argv[1]);

    // Init curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error initializing curl." << std::endl;
        exit(EXIT_FAILURE);
    }
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    //Create directories
    const std::filesystem::path outdir =
            std::filesystem::current_path() / std::filesystem::path("data/20130502");
    const std::filesystem::path siftdir = outdir / std::filesystem::path("sifting");
    const std::filesystem::path popdir = std::filesystem::path("data/populations");
    std::filesystem::create_directories(siftdir);
    std::filesystem::create_directories(popdir);

    // Download columns file
    const std::string columns_url =
            "https://raw.githubusercontent.com/pegasus-isi/1000genome-workflow/master/data/20130502/columns.txt";
    const std::filesystem::path columns_path = outdir / std::filesystem::path("columns.txt");
    download_data(curl, columns_url, columns_path);

    // Download populations files
    std::array<std::string, 7> populations{"ALL", "EUR", "EAS", "AFR", "AMR", "SAS", "GBR"};
    for(const std::string &name : populations) {
        const std::string pop_url =
                "https://raw.githubusercontent.com/pegasus-isi/1000genome-workflow/master/data/populations/" + name;
        const std::filesystem::path pop_path = popdir / std::filesystem::path(name);
        download_data(curl, pop_url, pop_path);
    }

    // For each chromosome
    for (int chr = 1; chr <= chrs; chr++) {

        // Download chromosome data from GitHub
        std::ostringstream out_name;
        out_name << "ALL.chr" << chr << ".250000.vcf.gz";
        const std::string url = "https://raw.githubusercontent.com/pegasus-isi/1000genome-workflow/master/data/20130502/" +
                                out_name.str();
        const std::filesystem::path out_path = outdir / std::filesystem::path(out_name.str());
        download_data(curl, url, out_path);
        decompress_data(out_path, out_path.parent_path() / out_path.stem());

        // Download sifting data
        std::ostringstream sift_name;
        sift_name << "ALL.chr" << chr << ".phase3_shapeit2_mvncall_integrated_v5.20130502.sites.annotation.vcf.gz";
        const std::string sift_url =
                "ftp://ftp.1000genomes.ebi.ac.uk/vol1/ftp/release/20130502/supporting/functional_annotation/filtered/" +
                sift_name.str();
        const std::filesystem::path sift_path = siftdir / std::filesystem::path(sift_name.str());
        download_data(curl, sift_url, sift_path);
        decompress_data(sift_path, sift_path.parent_path() / sift_path.stem());
    }

    // Close curl
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    // Stop timer and print
    std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    std::cout << "Done in " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
              << " seconds" << std::endl;

    // Return
    return 0;
}


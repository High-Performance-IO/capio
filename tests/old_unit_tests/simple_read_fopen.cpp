#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <mpi.h>
#include <sys/stat.h>
#include <unistd.h>

int sum_all(int *data, long int num_elements, long int num_reads, long int num_files) {
    int sum = 0;
    for (long int k = 0; k < num_reads * num_files; ++k) {
        for (long int i = 0; i < num_elements; ++i) {
            if (sum > std::numeric_limits<int>::max() - data[i + k * num_elements]) {
                sum = 0;
            }
            sum += data[i + k * num_elements];
        }
    }
    return sum;
}

using cclock = std::chrono::system_clock;
using sec    = std::chrono::duration<double>;

void read_from_file(int *data, long int num_elements, long int num_reads, int rank,
                    long int index) {
    std::string file_name = "file_" + std::to_string(rank) + "_" + std::to_string(index) + ".txt";
    FILE *fp              = fopen(file_name.c_str(), "r");
    if (fp == NULL) {
        std::cerr << "error impossible open file " << file_name << std::endl;
        MPI_Finalize();
        exit(1);
    }
    for (long int i = 0; i < num_reads; ++i) {
        long int k = 0;
        while (k < num_elements) {
            long int num_elements_read =
                fread(data + i * num_elements + k, sizeof(int), (num_elements - k), fp);
            k += num_elements_read;
            if (feof(fp)) {
                std::cerr << "fread error: reached EOF before then expected" << std::endl;
                MPI_Finalize();
                exit(1);
            } else if (ferror(fp)) {
                std::cerr << "fread error" << std::endl;
                MPI_Finalize();
                exit(1);
            }
        }
    }

    if (fclose(fp) == EOF) {
        std::cerr << "process " << rank << ", error closing the file\n";
    }
}

void read_from_files(int *data, long int num_elements, long int num_reads, long int num_files,
                     int rank, std::string time_file) {
    std::ofstream file;
    cclock::time_point before;
    if (rank == 0) {
        file.open(time_file, std::ios_base::app);
        // for milliseconds, use using ms = std::chrono::duration<double, std::milli>;
        before = cclock::now();
    }
    for (long int i = 0; i < num_files; ++i) {
        read_from_file(data + i * num_elements * num_reads, num_elements, num_reads, rank, i);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
        const sec duration = cclock::now() - before;
        file << "total read time: " << duration.count() << " secs" << std::endl;
        file.close();
    }
}

int main(int argc, char **argv) {
    int rank;
    long int num_elements, num_reads, num_files;
    std::string time_file;
    MPI_Init(&argc, &argv);
    if (argc != 5) {
        std::cerr << "simple read input error" << std::endl;
        MPI_Finalize();
        return 0;
    }
    num_elements = std::atol(argv[1]);
    num_reads    = std::atol(argv[2]);
    num_files    = std::atol(argv[3]);
    time_file    = argv[4];
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int *data = new int[num_elements * num_reads * num_files];
    read_from_files(data, num_elements, num_reads, num_files, rank, time_file);
    int sum = sum_all(data, num_elements, num_reads, num_files);
    std::cout << "process " << rank << ", sum = " << sum << "\n";
    delete[] data;
    MPI_Finalize();
}

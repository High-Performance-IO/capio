#include <fstream>
#include <iostream>
#include <mpi.h>



int main(int argc, char** argv) {
    int rank;
    MPI_Init(&argc, &argv);
    if (argc != 3) {
        std::cout << "input error: num of rows and num of columns needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::ifstream file("output_file_" + std::to_string(rank) + ".txt");
    if (! file.is_open()) {
        std::cout << "error opening file" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int sum = 0, num;
    for (int i = 0; i < num_rows; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            file >> num;
            sum += num;
        }
    }
    std::cout << "process " << rank << " sum = " << sum << std::endl;
    if (rank == 0)
        MPI_Reduce(MPI_IN_PLACE, &sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    else
        MPI_Reduce(&sum, nullptr, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "reduce result " << sum << std::endl;
    }
    file.close();
    MPI_Finalize();
}
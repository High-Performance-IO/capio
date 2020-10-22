#include <iostream>
#include <mpi.h>
#include "../capio_unordered.hpp"


/*
 * producers that write a sequence of strings using the capio proxy exploiting data transformation.
 * to use in combination with a consumer
 */

int const num_writes = 45;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int comm_size, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    capio_unordered<int> proxy("outputfile", comm_size, false, 10);
    std::cout << "after constuctor\n";
    for (int i = 0; i < num_writes; ++i) {
        proxy.write<std::string>(std::to_string(i), [](std::string str) {return std::stoi(str);});
        std::cout << "write process " << rank << std::endl;
    }
    std::cout << "before finished\n";
    proxy.finished();
    MPI_Finalize();
}

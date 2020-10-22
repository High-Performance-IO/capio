#include <iostream>
#include <mpi.h>
#include "../capio_unordered.hpp"

/*
 * consumers that read a sequence of integers and a sequence of structs through the capio proxy
 * to use in combination with a producer
 */

int const num_writes = 45;

void test_read(int num_producers, int rank) {
    capio_unordered<int> proxy("outputfile", num_producers, true, 10);
    if (rank == 0)
        std::cout << "process " << rank << " after constuctor" << std::endl;
    int i, actual_tot = 0, desidered_tot = 0;
    while (proxy.read(& i)) {
        actual_tot += i;
    }
    for (int prod = 0; prod < num_producers; ++prod) {
        for (int j = 0; j < num_writes; ++j) {
            desidered_tot += j;
        }
    }
    if (rank == 0)
        std::cout << "process " << rank << " actual_tot before reduce: " << actual_tot << std::endl;
    MPI_Allreduce(MPI_IN_PLACE, &actual_tot, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "process " << rank << " actual_tot: " << actual_tot <<std::endl;
        std::cout << "process " << rank << " desidered_tot: " << desidered_tot << std::endl;
    }
}

int main(int argc, char** argv) {
    int num_producers;
    int rank;
    MPI_Init(&argc, &argv);
    bool correct_input = true;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (argc != 2) {
        if (rank == 0)
            std::cout << "input error: launch the program passing the number of producers as argument" << std::endl;
        correct_input = false;
    }
    if (correct_input) {
        num_producers = std::stoi(argv[1]);
        test_read(num_producers, rank);
    }
    MPI_Finalize();
}
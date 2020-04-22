#include <iostream>
#include <cassert>
#include <mpi.h>
#include "../capio.hpp"

/*
 * single consumer that read a sequence of integers and a sequence of structs through the capio proxy
 * to use in combination with a producer
 */

struct elem {
    int i;
    int d;
    elem() {
        i = 0;
        d = 0;
    }
};

int const num_writes = 45;

void test_sequence_of_integers(int num_producers, int rank) {
    capio_proxy<int> proxy("outputfile", num_producers, true, 10);
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

void test_sequence_of_structs(int num_producers, int rank) {
    capio_proxy<struct elem> proxy2("outputfile2", num_producers, true, 12);
    if (rank == 0)
        std::cout << "process " << rank << " after constuctor" << std::endl;
    struct elem e;
    double desidered_tot = 0;
    double actual_tot = 0;
    while (proxy2.read(&e)) {
        actual_tot += e.i + e.d;
    }
    for (int prod = 0; prod < num_producers; ++prod) {
        for (int j = 0; j < num_writes; ++j) {
            desidered_tot += 2 * j + 1;
        }
    }
    if (rank == 0)
        std::cout << "process " << rank <<  "before actual_tot: " << actual_tot << std::endl;
    MPI_Allreduce(MPI_IN_PLACE, &actual_tot, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "process " << rank << " actual_tot: " << actual_tot << std::endl;
        std::cout << "process " << rank <<  "desidered_tot: " << desidered_tot << std::endl;
    }


}

int main(int argc, char** argv) {
    int num_producers = std::stoi(argv[1]);
    int rank;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    test_sequence_of_integers(num_producers, rank);
    test_sequence_of_structs(num_producers, rank);
    MPI_Finalize();
}



#include <iostream>
#include <mpi.h>
#include "../capio.hpp"

struct elem {
    int i;
    int d;
    elem() {
        i = 0;
        d = 0;
    }
};

/*
 * single producer that write a sequence of integers and a sequence of structs using the capio proxy
 * to use in combination with a consumer
 */

int const num_writes = 45;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    capio_proxy<int> proxy("outputfile", comm_size, false, 10);
    std::cout << "after constuctor\n";
    for (int i = 0; i < num_writes; ++i) {
        proxy.write(i);
        std::cout << "write\n";
    }
    std::cout << "before finished\n";
    proxy.finished();
    capio_proxy<struct elem> proxy2("outputfile2", comm_size, false, 12);
    std::cout << "after constuctor\n";
    struct elem e;
    for (int i = 0; i < num_writes; ++i) {
        e.i = i;
        e.d = i + 1;
        proxy2.write(e);
        std::cout << "write\n";
    }
    proxy2.finished();
    MPI_Finalize();
}

#include <iostream>
#include "../../queues/mpcs_queue.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "input error: num_rows and num_cols needed" << std::endl;
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    long long int dim = 1024 * 1024 * 1024 * 4LL;
    managed_shared_memory shm(open_or_create, "capio_shm",dim); //create only?
    const int buf_size = 10;
    mpsc_queue queue(shm, buf_size, 0, "capio");

    int* data = new int[num_rows * num_cols];
    for (int i = 0; i < num_rows * num_cols; ++i)
        queue.read(data + i, 1);
    for (int i = 0; i < num_rows * num_cols; ++i) {
        std::cout << data[i] << "\n";
    }
    queue.clean_shared_memory(shm);
    shared_memory_object::remove("capio_shm");
    free(data);
}
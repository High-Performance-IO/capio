#include "../capio_mpi/capio_mpi.hpp"
#include "../config_reader/config_reader.hpp"
#include <mpi.h>
#include <iostream>



int get_capio_dest(int dest, config_type &config) {
    int dest_machine = config["app2"][dest];
    std::unordered_map<int, int> capio_node = config["capio"];
    bool found = false;
    int i = 0;
    while (!found) {
        found = capio_node[i] == dest_machine;
        ++i;
    }
    return i;
}

void sender(mpsc_queue& queue, int* data, int rank, const std::string &config_path) {
    int size_next_msg;
    int dest, type;
    bool end = false;
    config_type config = get_deployment_config(config_path);
    while ( !end ) {
        std::cout << "sender before read" << std::endl;
        queue.read(&size_next_msg, 1);
        end = size_next_msg == -1;
        std::cout << "size_next_msg: " << size_next_msg << std::endl;
        if (!end) {
            queue.read(&dest, 1);
            data[0] = dest;
            queue.read(&type, 1);
            data[1] = type;
            queue.read(data + 2, size_next_msg);
            int capio_dest = get_capio_dest(dest, config);
            MPI_Send(data, size_next_msg + 2, MPI_INT, capio_dest, 1, MPI_COMM_WORLD);
        }

    }
    //to end the other capio process (only the size of the message is important)
    MPI_Send(&dest, 1, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
}

void listener(int* data, int num_consumers, const std::string &config_path) {
    int dest, type;
    int effective_size;
    bool end = false;
    MPI_Status status;
    capio_mpi capio(num_consumers, false, 1, config_path);
    while (!end) {
        std::cout << "listener before recv" << std::endl;
        MPI_Recv(data, 128, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        MPI_Get_count(&status, MPI_INT, &effective_size);
        std::cout << "listener effective_size: " << effective_size << std::endl;
        if (effective_size == 1) {
            end = true;
        } else {
            dest = data[0];
            type = data[1];
            if (type == 0) {
                capio.capio_send_proxy(data + 2, effective_size, dest, 0);
            }
            else {

                capio.capio_send_proxy(data + 2, effective_size, dest, 1);
            }
        }
    }
}


/*
 * two processes for each node
 */

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    std::string m_shm_name = "capio_shm";
    int rank;
    int data[128];
    if (argc != 3) {
        std::cout << "input error: number of consumers and config file needed " << std::endl;
        MPI_Finalize();
        return 0;
    }
    int num_cons = std::stoi(argv[1]);
    std::string config_path = argv[2];
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (rank % 2 == 0) {
        managed_shared_memory shm(open_or_create, m_shm_name.c_str(),65536); //create only?
        mpsc_queue queue(shm, 128, 0, true, "capio");
        sender(queue, data, rank, config_path);
        queue.clean_shared_memory(shm);
        std::cout << "sender terminated" << std::endl;
    }
    else {
        listener(data, num_cons, config_path);
        std::cout << "listener terminated" << std::endl;
    }
    MPI_Finalize();
}
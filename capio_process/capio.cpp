#include "../capio_ordered/capio_ordered.hpp"
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

void sender(mpsc_queue& queue, int rank, const std::string &config_path, int num_nodes) {
    int size_next_msg;
    int dest, type;
    int *data;
    bool end = false;
    config_type config = get_deployment_config(config_path);
    char str[50];
    int len, actual_size = 0;
    MPI_Get_processor_name(str, &len);
    std::string processor_name(str);
    std::cout << "sender " << std::to_string(rank) << processor_name << std::endl;
    while ( !end ) {
        std::cout << "sender before readr rank: "  << rank<< std::endl;
        queue.read(&size_next_msg, 1);
        end = size_next_msg == -1;
        std::cout << "sender size_next_msg: " << size_next_msg << " rank: "  << rank << std::endl;
        if (!end) {
            if (actual_size < (size_next_msg + 2)) {
                if (actual_size != 0)
                    free(data);
                data = new int[size_next_msg + 2];
                actual_size = size_next_msg + 2;
            }
            std::cout << "sender inside 1 if rank " << rank << std::endl;
            queue.read(&dest, 1);
            std::cout << "sender inside 2 if rank " << rank << std::endl;
            data[0] = dest;
            queue.read(&type, 1);
            std::cout << "sender inside 3 if rank " << rank << std::endl;
            data[1] = type;
            queue.read(data + 2, size_next_msg);
            int capio_dest = get_capio_dest(dest, config);
            std::cout << "sender capio process " << dest << " " << capio_dest << " rank: "  << rank << std::endl;
            MPI_Send(data, size_next_msg + 2, MPI_INT, capio_dest, 0, MPI_COMM_WORLD);
        }

    }
    if (actual_size != 0)
        free(data);
    //to end the other capio process (only the size of the message is important)
    for (int i = 0; i < num_nodes; ++i) {
        std::cout << "process capio rank " << rank << "send end msg to" << i * 2 + 1 << std::endl;
        MPI_Send(&dest, 1, MPI_INT, i * 2 + 1, 0, MPI_COMM_WORLD);
    }
}

void listener(const std::string &config_path, int rank, int num_nodes, int mpi_buf) {
    int dest, type;
    int effective_size;
    bool end = false;
    MPI_Status status;
    char str[50];
    int len;
    int* data = new int[mpi_buf];
    MPI_Get_processor_name(str, &len);
    std::string processor_name(str);
    std::cout << "listener " << std::to_string(rank) << processor_name << std::endl;
    std::cout << "capio process before create capio object rank: "  << rank << std::endl;
    capio_ordered capio(false, false, rank, config_path);
    std::cout << "capio process after create capio object rank: "  << rank << std::endl;
    int ended_capio_process = 0;
    while (!end) {
        std::cout << "listener before recv rank: "  << rank << std::endl;
        MPI_Recv(data, mpi_buf, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        std::cout << "before get count" << rank  << std::endl;
        MPI_Get_count(&status, MPI_INT, &effective_size);
        std::cout << "listener effective_size: " << effective_size << " rank: "  << rank << std::endl;
        if (effective_size == 1) {
            ++ended_capio_process;
            std::cout << "listener ended_capio_process: " << ended_capio_process << " rank: "  << rank << std::endl;
            if (ended_capio_process == num_nodes)
                end = true;
        }
        else {
            dest = data[0];
            type = data[1];
            effective_size = effective_size - 2;
            std::cout << "listener dest: " << dest << " rank: "  << rank << std::endl;
            if (type == 0) {
                std::cout << "capio send proxy 0 to data:" << data[2] << " rank: "  << rank << "dest " << dest << std::endl;
                capio.capio_send_proxy(data + 2, effective_size, dest, 0);
            }
            else {
                std::cout << "capio send proxy 1 data:" << data[2] <<  "rank: "  << rank << "dest " << dest  << std::endl;
                capio.capio_send_proxy(data + 2, effective_size, dest, 1);
            }
        }
    }
    free(data);
}


/*
 * two processes for each node
 */

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    std::string m_shm_name = "capio_shm";
    int rank;
    if (argc != 3) {
        std::cout << "input error: mpi_buf and config file needed " << std::endl;
        MPI_Finalize();
        return 1;
    }
    int mpi_buf = std::stoi(argv[1]);
    std::string config_path = argv[2];
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int num_nodes = get_num_nodes(get_deployment_config(config_path));
    std::cout << "num_nodes: " << num_nodes << std::endl;
    if (rank % 2 == 0) {
        long long int dim = 1024 * 1024 * 1024 * 4LL;
        managed_shared_memory shm(open_or_create, m_shm_name.c_str(),dim); //create only?
        mpsc_queue queue(shm, buf_size, 0, "capio");
        sender(queue, rank, config_path, num_nodes);
        queue.clean_shared_memory(shm);
        std::cout << "sender " << rank << " terminated" << std::endl;
    }
    else {
        listener(config_path, rank, num_nodes, mpi_buf);
        std::cout << "listener " << rank << " terminated" << std::endl;
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
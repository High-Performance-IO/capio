#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <vector>
#include <mpi.h>
#include "../queues/mpcs_queue.hpp"
#include "../config_reader/config_reader.hpp"

using namespace boost::interprocess;
const int buf_size = 1024 * 1024;


class capio_mpi {
private:
    managed_shared_memory m_shm;
    int* m_num_active_recipients_node;
    int* m_num_active_producers_node;
    int m_tot_num_recipients;
    int m_tot_num_producers;
    named_semaphore m_mutex_num_recipients;
    named_semaphore m_mutex_num_prods;
    named_semaphore m_sem_num_prods;
    std::unordered_map<int, mpsc_queue*> queues_recipients;
    std::unordered_map<int, mpsc_queue*> collective_queues_recipients;
    mpsc_queue* capio_queue;
    config_type config;
    int m_rank;
    bool m_recipient;
    bool m_producer;

    void capio_init() {
        std::string m_shm_name = "capio_shm";
        long long int dim = 1024 * 1024 * 1024 * 4LL;
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(),dim); //create only?
        int active_producers_node = get_num_processes_same_node("app1");
        int active_recipients_node = get_num_processes_same_node("app2");
        m_num_active_recipients_node = m_shm.find_or_construct<int>("num_recipients")(active_recipients_node);
        //std::cout << "debug 1" << std::endl;
        m_num_active_producers_node = m_shm.find_or_construct<int>("num_producers")(active_producers_node);
        //std::cout << "debug 2" << std::endl;

        capio_queue = new mpsc_queue(m_shm, buf_size, 0, "capio");
        //std::cout << "debug 3" << std::endl;
        std::vector<int> recipients_rank_same_node = get_recipients_rank_same_node();
        //std::cout << "debug 4" << std::endl;
        for (int rank : recipients_rank_same_node) {
           // std::cout << " m_rank and rank are in the same node" << m_rank << " " << rank << std::endl;
            queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, "norm");
            collective_queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, "coll");
        }
        //std::cout << "debug 5" << std::endl;

    }

    int get_id_capio_process_same_node() {
        int node;
        if (m_recipient) {
            node = config.at("app2").at(m_rank);
        }
        else if (m_producer) {
            node = config.at("app1").at(m_rank);
        }
        return node * 2;
    }

    int get_num_processes_same_node(std::string app) {
        int num_processes = 0;
        const std::unordered_map<int, int> &app_processes = config[app];
        int node;
   //     std::cout << "get_num_processes_same_node before if" << std::endl;
        if (m_recipient) {
            node = config.at("app2").at(m_rank);
        }
        else if (m_producer) {
            node = config.at("app1").at(m_rank);
        }
        else {
            node = config.at("capio").at(m_rank);
        }
        for (const auto& pair_k_v : app_processes) {
            if (pair_k_v.second == node)
                ++num_processes;
        }
    //    std::cout << "get_num_processes_same_node after if" << std::endl;
        return num_processes;
    }

    std::vector<int> get_recipients_rank_same_node() {
        std::vector<int> recipients;
        std::unordered_map<int, int> producers_allocation = config["app1"];
        std::unordered_map<int, int> recipients_allocation = config["app2"];
        std::unordered_map<int, int> capio_allocation = config["capio"];
        int node;
        if (m_recipient) {
            node = recipients_allocation[m_rank];
        }
        else if (m_producer) {
            node = producers_allocation[m_rank];
        }
        else {
            node = capio_allocation[m_rank];
        }
        for (auto pair_k_v : recipients_allocation) {
            if (pair_k_v.second == node)
                recipients.push_back(pair_k_v.first);
        }
        return recipients;
    }

    int set_num_processes(std::string app) {
        std::unordered_map<int, int> app_processes = config[app];
        return app_processes.size();
    }

    /*
     * for each node, the producer with minimum rank in that node sends a msg for terminating the capio processes
     * in that node
     */

    void terminate_capio_process() {
        std::unordered_map<int, int> prod_node_map = config["app1"];
        int i = 0;
        auto it = prod_node_map.find(i);
        int machine = 0;
        int prod_machine;
        int end_msg = -1;
        while (it != prod_node_map.end()) {
            prod_machine = it->second;
            if (prod_machine == machine) {
                if (m_rank == i) {
                    std::cout << "terminate capio prod_machine " << prod_machine << " machine " << machine << " m_rank "
                              << m_rank << " i " << i << std::endl;
                    capio_queue->write(&end_msg, 1);
                }
                ++machine;
            }
            ++i;
            it = prod_node_map.find(i);
        }
    }

    void capio_finalize() {
        if (m_recipient) {
            m_mutex_num_recipients.wait();
            --*m_num_active_recipients_node;
            if (*m_num_active_recipients_node == 0) {
                m_mutex_num_prods.wait();
                while (*m_num_active_producers_node > 0) {
                    m_mutex_num_prods.post();
                    m_sem_num_prods.wait();
                    m_mutex_num_prods.wait();
                }
                for (auto pair_k_v : queues_recipients) {
                    pair_k_v.second->clean_shared_memory(m_shm);
                }
                for (auto pair_k_v : collective_queues_recipients) {
                    pair_k_v.second->clean_shared_memory(m_shm);
                }
                shared_memory_object::remove("num_recipients");
                shared_memory_object::remove("num_producers");
                named_semaphore::remove("mutex_num_recipients_capio_shm");
                named_semaphore::remove("mutex_num_prods_capio_shm");
                named_semaphore::remove("sem_num_prods");
                shared_memory_object::remove("capio_shm");
            }
            else {
                free(capio_queue);
                for (auto pair_k_v : queues_recipients) {
                    free(pair_k_v.second);
                }
                for (auto pair_k_v : collective_queues_recipients) {
                    free(pair_k_v.second);
                }
                m_mutex_num_recipients.post();
                return;
            }
        }
        else if (m_producer){
            std::cout << "producer " << m_rank << " destroy capio object" << std::endl;
            terminate_capio_process();
        }
        free(capio_queue);
        for (auto pair_k_v : queues_recipients) {
            free(pair_k_v.second);
        }
        for (auto pair_k_v : collective_queues_recipients) {
            free(pair_k_v.second);
        }
        if (m_producer){
            m_mutex_num_prods.wait();
            --*m_num_active_producers_node;
            m_mutex_num_prods.post();
            m_sem_num_prods.post();
        }
    }

    int get_machine(int rank, bool recipient) {
        int machine;
        if (recipient) {
            machine = config["app2"][rank];
        }
        else {
            machine = config["app1"][rank];
        }
        return machine;
    }

    /*
     * called by the producer
     */

    bool same_machine(int rank) {
        return get_machine(m_rank, false) == get_machine(rank, true);
    }

    void capio_recv(int* data, int count, std::unordered_map<int, mpsc_queue*> &queues) {
        std::cout << "process " << m_rank << " before recv" << std::endl;
        queues[m_rank]->read(data, count);
        std::cout << "process " << m_rank << " after recv" << std::endl;

    }

    /*
     * if sender and recipient are in the same machine, the sender
     * write directly in the buffer of the recipient.
     * Otherwise the sender wrote in the buffer of the capio process.
     * The capio process will send the data to the capio process that resides
     * in the same machine of the recipient
     */

    void capio_send(int* data, int count, int rank, std::unordered_map<int, mpsc_queue*> &queues) {

        if (same_machine(rank)) {
            mpsc_queue* queue = queues[rank];
            std::cout << " same machine m_rank" << m_rank << " rank " << rank << std::endl;
            queue->write(data, count);
        }
        else {
            std::cout << " not same machine m_rank" << m_rank << " rank " << rank << std::endl;
            int tmp[count + 3];
            tmp[0] = count;
            tmp[1] = rank;
            if (&queues == &queues_recipients) {
                tmp[2] = 0;
                std::cout << "tmp[2]: " << tmp[2] << std::endl;
            }
            else {
                tmp[2] = 1;
                std::cout << "tmp[2]: " << tmp[2] << std::endl;
            }
            for (int i = 0; i < count; ++i) {
                tmp[i + 3] = data[i];
            }
            std::cout << "tmp[0]: " << tmp[0] << std::endl;
            std::cout << "count + 3 = " << count + 3 << std::endl;
            capio_queue->write(&tmp[0], count + 3);
            //std::cout << "not same machine after write" << std::endl;
        }
    }



    /*
     * return the rank of the producer with minimum rank that resides in the same machine
     * of the consumer with rank equals to root
     */

    int get_process_same_machine(int root) {
        int root_machine = get_machine(root, true);
        const std::unordered_map<int, int>& rank_node_map = config["app1"];
        int i = 0;
        int prod_machine;
        bool found = false;
        auto it = rank_node_map.find(i);
        while (it != rank_node_map.end() && !found) {
            prod_machine = it->second;
            found = prod_machine == root_machine;
            ++i;
            it = rank_node_map.find(i);
        }
        if (i > 0)
            --i;
        return i;
    }

public:

    capio_mpi(bool recipient, bool producer, int rank, std::string path) :
        m_mutex_num_recipients(open_or_create, "mutex_num_recipients_capio_shm", 1),
        m_sem_num_prods(open_or_create, "sem_num_prods", 0),
        m_mutex_num_prods(open_or_create, "mutex_num_prods_capio_shm", 1){
        m_recipient = recipient;
        m_producer = producer;
        m_rank = rank;
        config = get_deployment_config(path);
        m_tot_num_recipients = set_num_processes("app2");
        m_tot_num_producers = set_num_processes("app1");
        capio_init();
    }

    ~capio_mpi(){
        capio_finalize();
    }


    void capio_recv(int* data, int count) {
        capio_recv(data, count, queues_recipients);
    }

    void capio_send(int* data, int count, int rank) {
        capio_send(data, count, rank, queues_recipients);
    }

    void capio_send_proxy(int* data, int count, int rank, int type) {
        if (type == 0) {
            queues_recipients[rank]->write(data, count);
        }
        else {
            collective_queues_recipients[rank]->write(data, count);
        }
    }

    void capio_scatter(int* send_data, int* recv_data, int recv_count) {
        if (m_recipient) {
            capio_recv(recv_data, recv_count, collective_queues_recipients);
        }
        else {
                for (int j = 0; j < m_tot_num_recipients; ++j) {
                    capio_send(send_data + j * recv_count, recv_count, j, collective_queues_recipients);
                }
        }
    }

    void capio_gather(int* send_data, int send_count, int* recv_data, int recv_count, int root) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            std::cout << "capio_gather_balanced" << std::endl;
            capio_gather_balanced(send_data, send_count, recv_data, recv_count, root);
        }
        else {
            std::cout << "capio_gather_unbalanced" << std::endl;
            capio_gather_not_balanced(send_data, send_count, recv_data, recv_count, root);
        }
    }

    void capio_gather_balanced(int* send_data, int send_count, int* recv_data, int recv_count, int root) {
        if (m_recipient) {
            if (m_rank == root) {
                std::cout << "gather rank == root " <<  m_rank << std::endl;
                capio_recv(recv_data + root * recv_count / m_tot_num_producers, recv_count / m_tot_num_producers, collective_queues_recipients);
                std::cout << "before MPI_Gather " <<  m_rank << std::endl;
                MPI_Gather(MPI_IN_PLACE, 0, MPI_INT, recv_data, recv_count / m_tot_num_producers, MPI_INT, root, MPI_COMM_WORLD);
                std::cout << "after MPI_Gather " <<  m_rank << std::endl;
            }
            else {
                int* tmp = new int[recv_count / m_tot_num_producers];
                std::cout << "gather rank != root " <<  m_rank << std::endl;
                capio_recv(tmp, recv_count / m_tot_num_producers, collective_queues_recipients);
                std::cout << "before MPI_Gather " <<  m_rank << std::endl;
                MPI_Gather(tmp, recv_count / m_tot_num_producers, MPI_INT, nullptr, 0, MPI_INT, root, MPI_COMM_WORLD);
                std::cout << "after MPI_Gather " <<  m_rank << std::endl;
                free(tmp);
            }
        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);

            std::cout << "gather producer before capio_send " << m_rank << std::endl;
            capio_send(send_data, send_count, m_rank, collective_queues_recipients);
            std::cout << "gather producer after capio_send " << m_rank << std::endl;


            // to avoid that the two calls of capio_gather interfere with each other
            if (num_prods > 1) {
                if (rank == 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, num_prods - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else if (rank == num_prods - 1) {
                    MPI_Send(nullptr, 0, MPI_INT, 0, 0, MPI_COMM_WORLD);
                }
            }
        }
    }

    void capio_gather_not_balanced(int* send_data, int send_count, int* recv_data, int recv_count, int root) {
        if (m_recipient) {
            if (m_rank == root) {
                capio_recv(recv_data, recv_count, collective_queues_recipients);
            }
        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            if (rank > 0) {
                MPI_Recv(nullptr, 0, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            capio_send(send_data, send_count, root, collective_queues_recipients);
            if (rank < (num_prods - 1)) {
                MPI_Send(nullptr, 0, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
            }

            // to avoid that the two calls of capio_gather interfere with each other
            if (num_prods > 1) {
                if (rank == 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, num_prods - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else if (rank == num_prods - 1) {
                    MPI_Send(nullptr, 0, MPI_INT, 0, 0, MPI_COMM_WORLD);
                }
            }
        }
    }


    void capio_all_gather(int* send_data, int send_count, int* recv_data, int recv_count) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            capio_all_gather_balanced(send_data, send_count, recv_data, recv_count);
        }
        else {
            capio_all_gather_unbalanced(send_data, send_count, recv_data, recv_count);
        }
    }

    void capio_all_gather_balanced(int* send_data, int send_count, int* recv_data, int recv_count) {
        if (m_recipient) {
            std::cout << "capio_all_gather_balanced recv " <<  m_rank << " index " << m_rank * (recv_count / m_tot_num_producers) <<  std::endl;
            capio_recv(recv_data + m_rank * (recv_count / m_tot_num_producers), recv_count / m_tot_num_producers, collective_queues_recipients);
            std::cout << "before MPI_Gather " <<  m_rank << std::endl;
            MPI_Allgather(MPI_IN_PLACE, 0, MPI_INT, recv_data, recv_count / m_tot_num_producers, MPI_INT, MPI_COMM_WORLD);
            std::cout << "after MPI_Gather " <<  m_rank << std::endl;

        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);

            std::cout << "gather producer before capio_send " << m_rank << std::endl;
            capio_send(send_data, send_count, m_rank, collective_queues_recipients);
            std::cout << "gather producer after capio_send " << m_rank << std::endl;


            // to avoid that the two calls of capio_gather interfere with each other
            if (num_prods > 1) {
                if (rank == 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, num_prods - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else if (rank == num_prods - 1) {
                    MPI_Send(nullptr, 0, MPI_INT, 0, 0, MPI_COMM_WORLD);
                }
            }
        }
    }

    void capio_all_gather_unbalanced(int* send_data, int send_count, int* recv_data, int recv_count) {
        if (m_recipient) {
            capio_recv(recv_data, recv_count, collective_queues_recipients);
        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);

            for (int i = 0; i < m_tot_num_recipients; ++i) {
                if (rank > 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                std::cout << "all_gather process " << m_rank << " send to " << i << std::endl;
                capio_send(send_data, send_count, i, collective_queues_recipients);
                std::cout << "all_gather process " << m_rank << " after sent to " << i << std::endl;
                if (rank < (num_prods - 1)) {
                    MPI_Send(nullptr, 0, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
                }
            }
            // to avoid that the two calls of capio_gather interfere with each other
            if (num_prods > 1) {
                if (rank == 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, num_prods - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else if (rank == num_prods - 1) {
                    MPI_Send(nullptr, 0, MPI_INT, 0, 0, MPI_COMM_WORLD);
                }
            }
        }
    }


    void capio_reduce(int* send_data, int* recv_data, int count, MPI_Datatype data_type, void(*func)(void*, void*, int*, MPI_Datatype*), int root) {
        MPI_Op operation;
        int* tmp_buf;
        if (! m_recipient) {
            int size;
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            int process_same_machine = get_process_same_machine(root);
            MPI_Op_create(func, 1, &operation);
            tmp_buf = new int[count];
            MPI_Reduce(send_data, tmp_buf, count, data_type, operation, process_same_machine, MPI_COMM_WORLD);
            if (m_rank == process_same_machine) {
                capio_send(tmp_buf, count, root, collective_queues_recipients);
            }
            free(tmp_buf);
            MPI_Op_free(&operation);
        }
        else if (root == m_rank) {
            capio_recv(recv_data, count, collective_queues_recipients);
        }
    }

    void capio_all_reduce(int* send_data, int* recv_data, int count, MPI_Datatype data_type,
                          void(*func)(void*, void*, int*, MPI_Datatype*)) {
        if (m_tot_num_recipients == m_tot_num_producers) {
            capio_all_reduce_balanced(send_data, recv_data, count, data_type, func);
        }
        else {
            capio_all_reduce_unbalanced(send_data, recv_data, count, data_type, func);
        }
    }

    void capio_all_reduce_balanced(int* send_data, int* recv_data, int count, MPI_Datatype data_type,
                          void(*func)(void*, void*, int*, MPI_Datatype*)) {
        MPI_Op operation;
        if (! m_recipient) {
            int size;
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            MPI_Op_create(func, 1, &operation);
            MPI_Allreduce(MPI_IN_PLACE, send_data, count, data_type, operation, MPI_COMM_WORLD);
            capio_send(send_data, count, m_rank, collective_queues_recipients);
            MPI_Op_free(&operation);
        }
        else {
            capio_recv(recv_data, count, collective_queues_recipients);
        }

    }

    void capio_all_reduce_unbalanced(int* send_data, int* recv_data, int count, MPI_Datatype data_type,
                                   void(*func)(void*, void*, int*, MPI_Datatype*)) {
        MPI_Op operation;
        int* tmp_buf;
        if (! m_recipient) {
            int size;
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            std::vector<std::vector<int>> processes_same_machine(size);
            for (int i = 0; i < m_tot_num_recipients; ++i) {
                processes_same_machine[get_process_same_machine(i)].push_back(i);
            }
            MPI_Op_create(func, 1, &operation);
            tmp_buf = new int[count];
            MPI_Allreduce(send_data, tmp_buf, count, data_type, operation, MPI_COMM_WORLD);
            std::vector<int> recipients = processes_same_machine[m_rank];
            for (int recipient : recipients) {
                capio_send(tmp_buf, count, recipient, collective_queues_recipients);
            }
            free(tmp_buf);
            MPI_Op_free(&operation);
        }
        else {
            capio_recv(recv_data, count, collective_queues_recipients);
        }
    }


    /*
     * capio_all_to_all is an extension of capio_gather_all to the case where each process
     * sends distinct data to each of the receivers. The j-th block sent from process i is received
     * by process j and is placed in the i-th block of recv_data.
     */

    void capio_all_to_all(int* send_data, int send_count, int* recv_data) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            capio_all_to_all_balanced(send_data, send_count, recv_data);
        }
        else {
            capio_all_to_all_unbalanced(send_data, send_count, recv_data);
        }
    }


    void capio_all_to_all_balanced(int* send_data, int send_count, int* recv_data) {
        if (! m_recipient) {
            MPI_Alltoall(MPI_IN_PLACE, 0, MPI_INT, send_data, send_count, MPI_INT, MPI_COMM_WORLD);
            capio_send(send_data, send_count * m_tot_num_recipients, m_rank, collective_queues_recipients);
        }
        else {
            capio_recv(recv_data, send_count  * m_tot_num_recipients, collective_queues_recipients);
        }
    }

    void capio_all_to_all_unbalanced(int* send_data, int send_count, int* recv_data) {
        if (m_recipient) {
            for (int i = 0; i < m_tot_num_producers; ++i) {
                capio_recv(recv_data + i * send_count, send_count, collective_queues_recipients);
            }
        }
        else {
            int rank;
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            if (rank > 0) {
                MPI_Recv(nullptr, 0, MPI_INT, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            for (int i = 0; i < m_tot_num_recipients; ++i) {
                capio_send(send_data + i * send_count, send_count, i, collective_queues_recipients);
            }
            if (rank < (m_tot_num_producers - 1)) {
                MPI_Send(nullptr, 0, MPI_INT, rank + 1, 0, MPI_COMM_WORLD);
            }

            // to avoid that the two calls of capio_gather interfere with each other
            if (m_tot_num_producers > 1) {
                if (rank == 0) {
                    MPI_Recv(nullptr, 0, MPI_INT, m_tot_num_producers - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
                else if (rank == m_tot_num_producers - 1) {
                    MPI_Send(nullptr, 0, MPI_INT, 0, 0, MPI_COMM_WORLD);
                }
            }
        }
    }

    void capio_broadcast(int* buffer, int count, int root) {
        if (m_recipient) {
            capio_recv(buffer, count, collective_queues_recipients);
        }
        else {
            int rank;
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            if (rank == root) {
                for (int i = 0; i < m_tot_num_recipients; ++i) {
                    capio_send(buffer, count, i, collective_queues_recipients);
                }
            }
        }
    }

};
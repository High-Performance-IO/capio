#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <vector>
#include <mpi.h>
#include "../queues/mpcs_queue.hpp"
#include "../config_reader/config_reader.hpp"

using namespace boost::interprocess;
const int buf_size = 1024 * 1024;


class capio_ordered {
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
        m_num_active_producers_node = m_shm.find_or_construct<int>("num_producers")(active_producers_node);
        capio_queue = new mpsc_queue(m_shm, buf_size, 0, "capio");
        std::vector<int> recipients_rank_same_node = get_recipients_rank_same_node();
        for (int rank : recipients_rank_same_node) {
            queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, "norm");
            collective_queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, "coll");
        }
    }


    int get_num_processes_same_node(std::string app) {
        int num_processes = 0;
        const std::unordered_map<int, int> &app_processes = config[app];
        int node;
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
        queues[m_rank]->read(data, count);

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
            queue->write(data, count);
        }
        else {
            int tmp[count + 3];
            tmp[0] = count;
            tmp[1] = rank;
            if (&queues == &queues_recipients) {
                tmp[2] = 0;
            }
            else {
                tmp[2] = 1;
            }
            for (int i = 0; i < count; ++i) {
                tmp[i + 3] = data[i];
            }
            capio_queue->write(&tmp[0], count + 3);
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

    void capio_all_gather_balanced(int* send_data, int send_count, int* recv_data, int recv_count) {
        if (m_recipient) {
            capio_recv(recv_data + m_rank * (recv_count / m_tot_num_producers), recv_count / m_tot_num_producers, collective_queues_recipients);
            MPI_Allgather(MPI_IN_PLACE, 0, MPI_INT, recv_data, recv_count / m_tot_num_producers, MPI_INT, MPI_COMM_WORLD);

        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            capio_send(send_data, send_count, m_rank, collective_queues_recipients);


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
                capio_send(send_data, send_count, i, collective_queues_recipients);
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

    void capio_gather_balanced(int* send_data, int send_count, int* recv_data, int recv_count, int root) {
        if (m_recipient) {
            if (m_rank == root) {
                capio_recv(recv_data + root * recv_count / m_tot_num_producers, recv_count / m_tot_num_producers, collective_queues_recipients);
                MPI_Gather(MPI_IN_PLACE, 0, MPI_INT, recv_data, recv_count / m_tot_num_producers, MPI_INT, root, MPI_COMM_WORLD);
            }
            else {
                int* tmp = new int[recv_count / m_tot_num_producers];
                capio_recv(tmp, recv_count / m_tot_num_producers, collective_queues_recipients);
                MPI_Gather(tmp, recv_count / m_tot_num_producers, MPI_INT, nullptr, 0, MPI_INT, root, MPI_COMM_WORLD);
            }
        }
        else {
            int num_prods; //number of producers
            int rank;
            MPI_Comm_size(MPI_COMM_WORLD, &num_prods);
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);

            capio_send(send_data, send_count, m_rank, collective_queues_recipients);


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


public:

    capio_ordered(bool recipient, bool producer, int rank, std::string path) :
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

    ~capio_ordered(){
        capio_finalize();
    }

    /*
     * The consumer receives count elements into the buffer data
     *
     * inputs
     * int count: number of elements to receive
     *
     * outputs
     * int* data : starting address of receive buffer
     *
     * returns NONE
     */

    void capio_recv(int* data, int count) {
        capio_recv(data, count, queues_recipients);
    }

    /*
     * The producer send count element in the buffer data to the process of the second
     * application represented by the argument rank.
     *
     * inputs
     * int count: number of elements to send
     * int rank: rank of receiving process
     *
     * outputs
     * int* data : starting address of send buffer
     *
     * returns NONE
     */

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

    /*
     * The producer sends recv_count elements to each consumer. The send_data buffer
     * is divided in N blocks of recv_count elements where N is equals to the number
     * of consumers. The i-th block is sended to the i-th consumer.
     *
     * inputs
     * int recv_count: number of elements to receive
     *
     * outputs
     * int* send_data : starting address of sender buffer
     * int* recv_data: starting address of receiver buffer
     *
     * returns NONE
     */

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

    /*
     *
     * Each producer process sends send_count data stored in send_data buffer to the
     * consumer process with rank equals to root. The recv_data buffer is divided in
     * blocks of size equals to send_count. At the end, the i-th block will contain the
     * data of the i-th producer.
     *
     * inputs
     * int send_count: number of elements to send
     * int recv_count: number of elements for any single receive
     * int root: rank of the receiver
     *
     * outputs
     * int* send_data: starting address of send buffer
     * int* recv_data: starting address of receive buffer
     *
     * returns NONE
     */

    void capio_gather(int* send_data, int send_count, int* recv_data, int recv_count, int root) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            capio_gather_balanced(send_data, send_count, recv_data, recv_count, root);
        }
        else {
            capio_gather_not_balanced(send_data, send_count, recv_data, recv_count, root);
        }
    }



    /*
     *
     * This function  is an extension of capio_gather. All the consumers will receive the result of
     * the gather operation.
     *
     * inputs
     * int send_count: number of elements to send
     * int recv_count: number of elements for any single receive
     *
     * outputs
     * int* send_data: starting address of send buffer
     * int* recv_data: starting address of receive buffer
     *
     * returns NONE
     */

    void capio_all_gather(int* send_data, int send_count, int* recv_data, int recv_count) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            capio_all_gather_balanced(send_data, send_count, recv_data, recv_count);
        }
        else {
            capio_all_gather_unbalanced(send_data, send_count, recv_data, recv_count);
        }
    }



    /*
     * All the data of the producers in the buffer send data is reduced applying the
     * function pointed by the parameter func. For correct results func must be associative.
     *
     * inputs
     * int count:
     * MPI_Datatype data_type:
     * void(*func)(void*, void*, int*, MPI_Datatype*):
     * int root:
     *
     * outputs
     * int* send_data: starting address of send buffer
     * int* recv_data: starting address of receive buffer
     * int count: data to send to each receiver
     * MPI_Datatype data_type: data type of the elements used in the reduction function
     * void(*func)(void*, void*, int*, MPI_Datatype*): user defined function to perfrom the reduce
     * int root: rank of the process that wants the final result
     *
     * returns NONE
     */


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

    /*
     * This function is an extension of capio reduce. All the consumers will receive the
     * result of the reduction. For correct results func must be associative.
     *
     * inputs
     * int count: data to send to each receiver
     * MPI_Datatype data_type: data type of the elements used in the reduction function
     * void(*func)(void*, void*, int*, MPI_Datatype*): user defined function to perform the reduce
     *
     * outputs
     * int* send_data: starting address of send buffer
     * int* recv_data: starting address of receive buffer
     *
     * returns NONE
     */

    void capio_all_reduce(int* send_data, int* recv_data, int count, MPI_Datatype data_type,
                          void(*func)(void*, void*, int*, MPI_Datatype*)) {
        if (m_tot_num_recipients == m_tot_num_producers) {
            capio_all_reduce_balanced(send_data, recv_data, count, data_type, func);
        }
        else {
            capio_all_reduce_unbalanced(send_data, recv_data, count, data_type, func);
        }
    }




    /*
     * this function extends capio_all_gather allowing each producer to
     * sends distinct data to each of the receivers. The buffers send data and recv data
     * are logically divided in blocks of size send count. The i-th block of the j-th
     * producer is sent to the i-th consumer and is placed in its j-th block. All the
     * blocks have the same size.
     *
     * inputs
     * int send_count: number of elements to send to each consumer
     *
     * outputs
     * int* send_data: starting address of sender buffer
     * int* recv_data: starting address of receiver buffer
     *
     * returns NONE
     */

    void capio_all_to_all(int* send_data, int send_count, int* recv_data) {
        if (m_tot_num_producers == m_tot_num_recipients) {
            capio_all_to_all_balanced(send_data, send_count, recv_data);
        }
        else {
            capio_all_to_all_unbalanced(send_data, send_count, recv_data);
        }
    }




    /*
     * The producer with rank equals to root sends count elements that reside in buffer
     * to all the consumers. The consumers stores the data in buffer
     *
     * inputs
     * int count: number of elements in buffer
     * int root: rank of the producer that sends the data
     *
     * outputs
     * int* buffer: starting address of buffer
     *
     * returns NONE
     */

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
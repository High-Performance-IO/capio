#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <vector>
#include <mpi.h>
#include "../queues/mpcs_queue.hpp"
#include "../config_reader/config_reader.hpp"

using namespace boost::interprocess;
const int buf_size = 128;


class capio_mpi {
private:
    managed_shared_memory m_shm;
    int* m_num_active_recipients_node;
    int* m_num_active_producers_node;
    int m_tot_num_recipients;
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
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(),65536); //create only?
        int active_producers_node = get_num_processes_same_node("app1");
        int active_recipients_node = get_num_processes_same_node("app2");
        m_num_active_recipients_node = m_shm.find_or_construct<int>("num_recipients")(active_recipients_node);
        m_num_active_producers_node = m_shm.find_or_construct<int>("num_producers")(active_producers_node);
        capio_queue = new mpsc_queue(m_shm, 128, 0, false, "capio");
        std::vector<int> recipients_rank_same_node = get_recipients_rank_same_node();
        for (int rank : recipients_rank_same_node) {
            queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, m_recipient, "norm");
            collective_queues_recipients[rank] = new mpsc_queue(m_shm, buf_size, rank, m_recipient, "coll");
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
        int machine = -1;
        int prod_machine;
        int end_msg = -1;
        while (it != prod_node_map.end()) {
            prod_machine = it->second;
            if (prod_machine != machine) {
                capio_queue->write(&end_msg, 1);
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
        mpsc_queue* queue = queues[rank];
        if (same_machine(rank)) {
            queue->write(data, count);
        }
        else {
            int tmp[count + 3];
            tmp[0] = count;
            tmp[1] = rank;
            if (queue == queues_recipients[rank]) {
                tmp[2] = 0;
            }
            else {
                tmp[2] = 1;
            }
            for (int i = 0; i < count; ++count) {
                tmp[i + 3] = data[i];
            }
            capio_queue->write(tmp, count);
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

    void capio_all_to_all(int* send_data, int send_count, int* recv_data, int num_prods) {
        if (m_recipient) {
            for (int i = 0; i < num_prods; ++i) {
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
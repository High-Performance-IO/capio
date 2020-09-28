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
    int* m_num_active_recipients;
    int* m_num_active_producers;
    int m_num_recipients;
    int m_num_producers;
    named_semaphore m_mutex_num_recipients;
    named_semaphore m_mutex_num_prods;
    named_semaphore m_sem_num_prods;
    mpsc_queue** queues_recipients;
    mpsc_queue** collective_queues_recipients;
    mpsc_queue* capio_queue;
    config_type config;
    int m_rank;
    bool m_recipient;
    bool m_producer;

    void capio_init(int num_recipients) {
        std::string m_shm_name = "capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(),65536); //create only?
        m_num_active_recipients = m_shm.find_or_construct<int>("num_recipients")(num_recipients);
        m_num_active_producers = m_shm.find_or_construct<int>("num_producers")(m_num_producers);
        capio_queue = new mpsc_queue(m_shm, 128, 0, false, "capio");
        queues_recipients = new mpsc_queue*[num_recipients];
        collective_queues_recipients = new mpsc_queue*[num_recipients];
        for (int i = 0; i < num_recipients; ++i) {
            queues_recipients[i] = new mpsc_queue(m_shm, buf_size, i, m_recipient, "norm");
            collective_queues_recipients[i] = new mpsc_queue(m_shm, buf_size, i, m_recipient, "coll");
        }

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
            --*m_num_active_recipients;
            if (*m_num_active_recipients == 0) {
                m_mutex_num_prods.wait();
                while (*m_num_active_producers > 0) {
                    m_mutex_num_prods.post();
                    m_sem_num_prods.wait();
                    m_mutex_num_prods.wait();
                }
                for (int i = 0; i < m_num_recipients; ++i) {
                    queues_recipients[i]->clean_shared_memory(m_shm);
                    collective_queues_recipients[i]->clean_shared_memory(m_shm);
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
                for (int i = 0; i < m_num_recipients; ++i) {
                    free(queues_recipients[i]);
                    free(collective_queues_recipients[i]);
                }
                free(queues_recipients);
                free(collective_queues_recipients);
                m_mutex_num_recipients.post();
                return;
            }
        }
        else if (m_producer){
            terminate_capio_process();
        }
        free(capio_queue);
        for (int i = 0; i < m_num_recipients; ++i) {
            free(queues_recipients[i]);
            free(collective_queues_recipients[i]);
        }
        free(queues_recipients);
        free(collective_queues_recipients);
        if (m_producer){
            m_mutex_num_prods.wait();
            --*m_num_active_producers;
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

    void capio_recv(int* data, int count, mpsc_queue** queues) {
        queues[m_rank]->read(data, count);
    }

    /*
     * if sender and recipient are in the same machine, the sender
     * write directly in the buffer of the recipient.
     * Otherwise the sender wrote in the buffer of the capio process.
     * The capio process will send the data to the capio process that resides
     * in the same machine of the recipient
     */

    void capio_send(int* data, int count, int rank, mpsc_queue** queues) {
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

    capio_mpi(int num_recipients, int num_producers, bool recipient, bool producer, int rank, std::string path) :
        m_mutex_num_recipients(open_or_create, "mutex_num_recipients_capio_shm", 1),
        m_sem_num_prods(open_or_create, "sem_num_prods", 0),
        m_mutex_num_prods(open_or_create, "mutex_num_prods_capio_shm", 1){
        m_recipient = recipient;
        m_producer = producer;
        m_num_producers = num_producers;
        m_rank = rank;
        capio_init(num_recipients);
        m_num_recipients = num_recipients;
        config = get_deployment_config(path);
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
                for (int j = 0; j < m_num_recipients; ++j) {
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

            for (int i = 0; i < m_num_recipients; ++i) {
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


    void capio_reduce(int* send_data, int* recv_data, int count, MPI_Datatype data_type, void(*func)(void*, void*, int*, MPI_Datatype*), int root, int prod_rank) {
        MPI_Op operation;
        int* tmp_buf;
        if (! m_recipient) {
            int size;
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            int process_same_machine = get_process_same_machine(root);
            MPI_Op_create(func, 1, &operation);
            tmp_buf = new int[count];
            MPI_Reduce(send_data, tmp_buf, count, data_type, operation, process_same_machine, MPI_COMM_WORLD);
            if (prod_rank == process_same_machine) {
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
                          void(*func)(void*, void*, int*, MPI_Datatype*), int prod_rank) {
        MPI_Op operation;
        int* tmp_buf;
        if (! m_recipient) {
            int size;
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            std::vector<std::vector<int>> processes_same_machine(size);
            for (int i = 0; i < m_num_recipients; ++i) {
                processes_same_machine[get_process_same_machine(i)].push_back(i);
            }
            MPI_Op_create(func, 1, &operation);
            tmp_buf = new int[count];
            MPI_Allreduce(send_data, tmp_buf, count, data_type, operation, MPI_COMM_WORLD);
            std::vector<int> recipients = processes_same_machine[prod_rank];
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
            for (int i = 0; i < m_num_recipients; ++i) {
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
                for (int i = 0; i < m_num_recipients; ++i) {
                    capio_send(buffer, count, i, collective_queues_recipients);
                }
            }
        }
    }

};
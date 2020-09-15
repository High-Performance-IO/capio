#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include "../queues/mpcs_queue.hpp"

using namespace boost::interprocess;
const int buf_size = 128;


class capio_mpi {
private:
    managed_shared_memory m_shm;
    int* m_num_active_recipients;
    int m_num_recipients;
    named_semaphore m_mutex_num_recipients;
    mpsc_queue** queues_recipients;
    mpsc_queue** collective_queues_recipients;
    int m_rank;
    bool m_recipient;

    void capio_init(int num_recipients) {
        std::string m_shm_name = "capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(),65536); //create only?
        m_num_active_recipients = m_shm.find_or_construct<int>("num_recipients")(num_recipients);
        queues_recipients = new mpsc_queue*[num_recipients];
        collective_queues_recipients = new mpsc_queue*[num_recipients];
        for (int i = 0; i < num_recipients; ++i) {
            queues_recipients[i] = new mpsc_queue(m_shm, buf_size, i, m_recipient, "norm");
            collective_queues_recipients[i] = new mpsc_queue(m_shm, buf_size, i, m_recipient, "coll");
        }

    }

    void capio_finalize() {
        if (m_recipient) {
            m_mutex_num_recipients.wait();
            --*m_num_active_recipients;
            m_mutex_num_recipients.post();
            queues_recipients[m_rank]->clean_shared_memory(m_shm);
            collective_queues_recipients[m_rank]->clean_shared_memory(m_shm);
            if (*m_num_active_recipients == 0) {
                shared_memory_object::remove("num_recipients");
                named_semaphore::remove("mutex_num_recipients_capio_shm");
                shared_memory_object::remove("capio_shm");
            }
        }
        for (int i = 0; i < m_num_recipients; ++i) {
            free(queues_recipients[i]);
            free(collective_queues_recipients[i]);
        }
        free(queues_recipients);
        free(collective_queues_recipients);
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
        queues[rank]->write(data, count);
    }

    /*
     * return the rank of a producer that resides in the same machine
     * of the consumer with rank equals to root
     */

    int get_process_same_machine(int root) {
        return root;
    }

public:

    capio_mpi(int num_recipients, bool recipient, int rank = 0) :
        m_mutex_num_recipients(open_or_create, "mutex_num_recipients_capio_shm", num_recipients) {
        m_recipient = recipient;
        m_rank = rank;
        capio_init(num_recipients);
        m_num_recipients = num_recipients;
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
        int process_same_machine = get_process_same_machine(root);
        if (! m_recipient) {
            MPI_Op_create(func, 1, &operation);
            tmp_buf = new int[count];
            std::cout << "process rank " << prod_rank << "before reduce" << std::endl;
            MPI_Reduce(send_data, tmp_buf, count, data_type, operation, process_same_machine, MPI_COMM_WORLD);
            std::cout << "process rank " << prod_rank << "after reduce" << std::endl;
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


};
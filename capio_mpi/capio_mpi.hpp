#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <string>

using namespace boost::interprocess;
const int buf_size = 128;


class capio_mpi {
private:
    managed_shared_memory m_shm;
    int* m_num_active_recipients;
    int m_num_recipients;
    named_semaphore m_mutex_num_recipients;
    named_semaphore** m_buffer_mutex_prods_array;
    named_semaphore** m_buffers_num_empty;
    named_semaphore** m_buffers_num_stored;
    int** buffers_recipients;
    int* prod_indexes;
    int* cons_indexes;
    int m_rank;
    bool m_recipient;

    void capio_init(int num_recipients) {

        std::string m_shm_name = "capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(),65536); //create only?
        m_num_active_recipients = m_shm.find_or_construct<int>("num_recipients")(num_recipients);
        if (m_recipient) {
            cons_indexes = m_shm.find_or_construct<int>("cons_indexes" )[num_recipients](0);
        }
        else {
            prod_indexes = m_shm.find_or_construct<int>("prod_indexes" )[num_recipients](0);
        }
        for (int i = 0; i < num_recipients; ++i) {
            int* buffer = m_shm.find_or_construct<int>(("consumer_buffer" + std::to_string(i)).c_str())[buf_size]();
            const char* buffer_mutex_name = ("mutex_buffer_" + std::to_string(i)).c_str();
            m_buffer_mutex_prods_array[i] = new named_semaphore(open_or_create, buffer_mutex_name, 1);
            const char* num_empty_mutex_name = ("mutex_empty_" + std::to_string(i)).c_str();
            m_buffers_num_empty[i] = new named_semaphore(open_or_create, num_empty_mutex_name, buf_size);
            const char* num_stored_mutex_name = ("mutex_stored_" + std::to_string(i)).c_str();
            m_buffers_num_stored[i] = new named_semaphore(open_or_create, num_stored_mutex_name, 0);
            buffers_recipients[i] = buffer;
        }

    }

    void capio_finalize() {
        for (int i = 0; i < m_num_recipients; ++i) {
            free(m_buffer_mutex_prods_array[i]);
            free(m_buffers_num_empty[i]);
            free(m_buffers_num_stored[i]);
        }
        free(m_buffer_mutex_prods_array);
        free(m_buffers_num_empty);
        free(m_buffers_num_stored);
        free(buffers_recipients);

        if (m_recipient) {
            m_mutex_num_recipients.wait();
            --*m_num_active_recipients;
            m_mutex_num_recipients.post();
            int* m_buffer = m_shm.find<int>(("consumer_buffer" + std::to_string(m_rank)).c_str()).first;
            m_shm.destroy_ptr(m_buffer);
            named_semaphore::remove(("mutex_buffer_" + std::to_string(m_rank)).c_str());
            named_semaphore::remove(("mutex_empty_" + std::to_string(m_rank)).c_str());
            named_semaphore::remove(("mutex_stored_" + std::to_string(m_rank)).c_str());
            if (*m_num_active_recipients == 0) {
                shared_memory_object::remove("num_recipients");
                shared_memory_object::remove("prod_indexes");
                shared_memory_object::remove("cons_indexes");
                named_semaphore::remove("mutex_num_recipients_capio_shm");
                shared_memory_object::remove("capio_shm");
            }
        }
    }

public:

    capio_mpi(int num_recipients, bool recipient, int rank = 0) :
        m_mutex_num_recipients(open_or_create, "mutex_num_recipients_capio_shm", num_recipients) {
        m_recipient = recipient;
        m_rank = rank;
        m_buffer_mutex_prods_array = new named_semaphore*[num_recipients];
        m_buffers_num_empty = new named_semaphore*[num_recipients];
        m_buffers_num_stored = new named_semaphore*[num_recipients];
        buffers_recipients = new int*[num_recipients];
        capio_init(num_recipients);
        m_num_recipients = num_recipients;
    }

    ~capio_mpi(){
        capio_finalize();
    }

    void capio_recv(int* data, int count) {
        for (int i = 0; i < count; ++i) {
            m_buffers_num_stored[m_rank]->wait();
            *data =  buffers_recipients[m_rank][cons_indexes[m_rank]];
            m_buffers_num_empty[m_rank]->post();
            if (cons_indexes[m_rank] < buf_size - 1) {
                ++cons_indexes[m_rank];
            }
            else {
                cons_indexes[m_rank] = 0;
            }
        }
    }


/*
 * if sender and recipient are in the same machine, the sender
 * write directly in the buffer of the recipient.
 * Otherwise the sender wrote in the buffer of the capio process.
 * The capio process will send the data to the capio process that resides
 * in the same machine of the recipient
 */

    void capio_send(int* data, int count, int rank) {
        m_buffer_mutex_prods_array[rank]->wait();
        int j = prod_indexes[rank];
        for (int i = 0; i < count; ++i) {
            m_buffers_num_empty[rank]->wait();
            buffers_recipients[rank][j] = data[i];
            m_buffers_num_stored[rank]->post();
            if (prod_indexes[rank] < buf_size - 1) {
                ++prod_indexes[rank];
            }
            else {
                prod_indexes[rank] = 0;
            }
        }
        m_buffer_mutex_prods_array[rank]->post();
    }

};
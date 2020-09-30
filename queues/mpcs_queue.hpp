#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <iostream>
/*
 * multi producer - single consumer queue
 */

using namespace boost::interprocess;

class mpsc_queue {
private:
    named_semaphore* m_buffer_mutex;
    named_semaphore* m_buffer_empty;
    named_semaphore* m_buffer_store;

    int m_buf_size;

    int* m_buffer;
    int* m_prod_index;
    int* m_cons_index;

    std::string buffer_mutex_name;
    std::string num_empty_mutex_name;
    std::string num_stored_mutex_name;
    std::string prod_index_memory_name;
    std::string cons_index_memory_name;


public:

    mpsc_queue(managed_shared_memory &shm, int buf_size, int i, bool cons, std::string queue_name) {
        m_buf_size = buf_size;
        m_buffer = shm.find_or_construct<int>((queue_name + "cons_buf" + std::to_string(i)).c_str())[m_buf_size]();
        buffer_mutex_name = (queue_name + "mtx_buf_" + std::to_string(i));
        m_buffer_mutex = new named_semaphore(open_or_create, buffer_mutex_name.c_str(), 1);
        num_empty_mutex_name = (queue_name + "mtx_empty_" + std::to_string(i)).c_str();
        m_buffer_empty = new named_semaphore(open_or_create, num_empty_mutex_name.c_str(), 0);
        num_stored_mutex_name = (queue_name + "mtx_strd_" + std::to_string(i)).c_str();
        m_buffer_store = new named_semaphore(open_or_create, num_stored_mutex_name.c_str(), 1);

        cons_index_memory_name = (queue_name + "cons_i_" + std::to_string(i));
        prod_index_memory_name = (queue_name + "prod_i_" + std::to_string(i));
        m_cons_index = shm.find_or_construct<int>(cons_index_memory_name.c_str())(0);
        m_prod_index = shm.find_or_construct<int>(prod_index_memory_name.c_str())();
    }

    ~mpsc_queue() {
        free(m_buffer_mutex);
        free(m_buffer_empty);
        free(m_buffer_store);
    }

    /*
     * only the consumer has to call this function in order to free the shared memory
     */

    void clean_shared_memory(managed_shared_memory &shm) {
        shm.destroy_ptr(m_buffer);
        named_semaphore::remove(buffer_mutex_name.c_str());
        named_semaphore::remove(num_empty_mutex_name.c_str());
        named_semaphore::remove(num_stored_mutex_name.c_str());
        shared_memory_object::remove(cons_index_memory_name.c_str());
        shared_memory_object::remove(prod_index_memory_name.c_str());
    }

    void read(int *data, int count) {
        m_buffer_empty->wait();
        for (int i = 0; i < count; ++i) {
            if (*m_cons_index == *m_prod_index) {
                m_buffer_store->post();
                m_buffer_empty->wait();
            }
            data[i] =  m_buffer[*m_cons_index];
            *m_cons_index = (*m_cons_index + 1) % m_buf_size;
        }
        m_buffer_store->post();
    }

    void write(int* data, int count) {
        m_buffer_mutex->wait();
        m_buffer_store->wait();
        for (int i = 0; i < count; ++i) {
            if (((*m_prod_index + 1) % m_buf_size) == *m_cons_index) {
                m_buffer_empty->post();
                m_buffer_store->wait();
            }
            m_buffer[*m_prod_index] = data[i];
            *m_prod_index = (*m_prod_index + 1) % m_buf_size;
        }
        m_buffer_empty->post();
        m_buffer_mutex->post();
    }


};
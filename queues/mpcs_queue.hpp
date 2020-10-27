#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>

#include <boost/interprocess/sync/scoped_lock.hpp>
#include <iostream>
/*
 * multi producer - single consumer queue
 */

using namespace boost::interprocess;

class mpsc_queue {
private:
    named_semaphore* m_buffer_mutex;
    named_mutex* cons_prod_mutex;
    named_semaphore* cond_empty;
    named_semaphore* cond_full;

    int m_buf_size;

    int* m_buffer;
    int* m_prod_index;
    int* m_cons_index;
    bool* yet_post_full;
    bool* yet_post_empty;

    std::string buffer_mutex_name;
    std::string prod_index_memory_name;
    std::string cons_index_memory_name;
    std::string cons_prod_mutex_name;
    std::string cond_empty_name;
    std::string cond_full_name;
    std::string yet_post_full_name;
    std::string yet_post_empty_name;


public:

    mpsc_queue(managed_shared_memory &shm, int buf_size, int i, std::string queue_name) {
        m_buf_size = buf_size;
        m_buffer = shm.find_or_construct<int>((queue_name + "cons_buf" + std::to_string(i)).c_str())[m_buf_size]();
        buffer_mutex_name = (queue_name + "mtx_buf_" + std::to_string(i));
        m_buffer_mutex = new named_semaphore(open_or_create, buffer_mutex_name.c_str(), 1);

        cons_prod_mutex_name = (queue_name + "mtx_prdcns_" + std::to_string(i)).c_str();
        cons_prod_mutex = new named_mutex(open_or_create, cons_prod_mutex_name.c_str());
        cond_empty_name = (queue_name + "cnd_empty_" + std::to_string(i)).c_str();
        cond_empty = new named_semaphore(open_or_create, cond_empty_name.c_str(), 0);
        cond_full_name = (queue_name + "cnd_full_" + std::to_string(i)).c_str();
        cond_full = new named_semaphore(open_or_create, cond_full_name.c_str(), 0);

        cons_index_memory_name = (queue_name + "cons_i_" + std::to_string(i));
        prod_index_memory_name = (queue_name + "prod_i_" + std::to_string(i));
        m_cons_index = shm.find_or_construct<int>(cons_index_memory_name.c_str())(0);
        m_prod_index = shm.find_or_construct<int>(prod_index_memory_name.c_str())(0);
        yet_post_full_name = (queue_name + "post_f_" + std::to_string(i));
        yet_post_full = shm.find_or_construct<bool>(yet_post_full_name.c_str())(true);
        yet_post_empty_name = (queue_name + "post_e_" + std::to_string(i));
        yet_post_empty = shm.find_or_construct<bool>(yet_post_empty_name.c_str())(true);
    }

    ~mpsc_queue() {
        free(m_buffer_mutex);
        free(cons_prod_mutex);
        free(cond_empty);
        free(cond_full);

    }

    /*
     * only the consumer has to call this function in order to free the shared memory
     */

    void clean_shared_memory(managed_shared_memory &shm) {
        shm.destroy_ptr(m_buffer);
        shm.destroy_ptr(yet_post_empty);
        shm.destroy_ptr(yet_post_full);
        shm.destroy_ptr(m_prod_index);
        shm.destroy_ptr(m_cons_index);
        named_semaphore::remove(buffer_mutex_name.c_str());
        shared_memory_object::remove(cons_index_memory_name.c_str());
        shared_memory_object::remove(prod_index_memory_name.c_str());
        named_mutex::remove(cons_prod_mutex_name.c_str());
        named_semaphore::remove(cond_empty_name.c_str());
        named_semaphore::remove(cond_full_name.c_str());

    }

    void read(int *data, int count) {
        cons_prod_mutex->lock();
        for (int i = 0; i < count; ++i) {
            if (*m_prod_index == *m_cons_index) {
                if (! *yet_post_empty) {
                    *yet_post_empty = true;
                    cond_empty->post();
                }
                *yet_post_full = false;
                cons_prod_mutex->unlock();
                cond_full->wait();
                cons_prod_mutex->lock();
            }
            data[i] =  m_buffer[*m_cons_index];
            *m_cons_index = (*m_cons_index + 1) % m_buf_size;
        }
        if (! *yet_post_empty) {
            *yet_post_empty = true;
            cond_empty->post();
        }
        cons_prod_mutex->unlock();
    }

    void write(int* data, int count) {
        m_buffer_mutex->wait();
        cons_prod_mutex->lock();
        for (int i = 0; i < count; ++i) {
            if ((*m_prod_index + 1) % m_buf_size == *m_cons_index) {
                if (! *yet_post_full) {
                    *yet_post_full = true;
                    cond_full->post();
                }
                *yet_post_empty = false;
                cons_prod_mutex->unlock();
                cond_empty->wait();
                cons_prod_mutex->lock();

            }
            m_buffer[*m_prod_index] = data[i];
            *m_prod_index = (*m_prod_index + 1 ) % m_buf_size;
        }
        if (! *yet_post_full) {
            *yet_post_full = true;
            cond_full->post();
        }
        cons_prod_mutex->unlock();
        m_buffer_mutex->post();
    }


};
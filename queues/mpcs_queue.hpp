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
        //std::cout << "buf_size " << m_buf_size << std::endl;
        //std::cout << "5 % buf_size = " << (5 % buf_size) << std::endl;
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
       // free(m_prod_index);
       // free(m_cons_index);

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
       //ls std::cout << "read mpsc " << buffer_mutex_name << std::endl;

        //std::cout << "cons_index_memory_name: " << cons_index_memory_name  << "*m_cons_index: " << *m_cons_index  << std::endl;
       // std::cout << "*m_cons_index: " << *m_cons_index << std::endl;
       // std::cout << "prod_index_memory_name: " << prod_index_memory_name << std::endl;
       // std::cout << "count: " << count << std::endl;


       // std::cout << "reader before lock " << buffer_mutex_name << std::endl;
        cons_prod_mutex->lock();
        //std::cout << "reader after lock " << buffer_mutex_name << std::endl;
        for (int i = 0; i < count; ++i) {
           // std::cout << "read i " << i << " " << buffer_mutex_name << std::endl;
            //std::cout << "*m_prod_index " << *m_prod_index << " *m_cons_index " << *m_cons_index <<  " " << buffer_mutex_name <<std::endl;
            if (*m_prod_index == *m_cons_index) {
             //  std::cout << "buf is empty " << buffer_mutex_name << std::endl;
                if (! *yet_post_empty) {
                //    std::cout << " cond_empty->post();; " << buffer_mutex_name << std::endl;
                    *yet_post_empty = true;
                //    std::cout << "debug 1 " << buffer_mutex_name << std::endl;
                    cond_empty->post();
               //     std::cout << "debug 2 " << buffer_mutex_name << std::endl;
                }
              //  std::cout << "debug 3 " << buffer_mutex_name << std::endl;
                *yet_post_full = false;
              //  std::cout << "before unlock " << buffer_mutex_name << std::endl;
                cons_prod_mutex->unlock();
              //  std::cout << "before full wait " << buffer_mutex_name << std::endl;
                cond_full->wait();
              //  std::cout << "after full wait " << buffer_mutex_name << std::endl;
                cons_prod_mutex->lock();
               // std::cout << "*m_prod_index " << *m_prod_index << " *m_cons_index " << *m_cons_index << std::endl;
            }
            data[i] =  m_buffer[*m_cons_index];
           // std::cout << "elem " << data[i] << " " << buffer_mutex_name << std::endl;
            *m_cons_index = (*m_cons_index + 1) % m_buf_size;
        }
      //  std::cout << "reader before end " << buffer_mutex_name << std::endl;
        if (! *yet_post_empty) {
          //  std::cout << " cond_empty->post();; " << buffer_mutex_name << std::endl;
            *yet_post_empty = true;
            cond_empty->post();
        }
        cons_prod_mutex->unlock();
      //  std::cout << "reader after end " << buffer_mutex_name << std::endl;
    }

    void write(int* data, int count) {
      //  std::cout << "write mpsc " << buffer_mutex_name << std::endl;
        //std::cout << "cons_index_memory_name: " << cons_index_memory_name << std::endl;
       // std::cout << "prod_index_memory_name: " << prod_index_memory_name << "*m_prod_index: " << *m_prod_index << std::endl;
        m_buffer_mutex->wait();

       // std::cout << "write before lock " << buffer_mutex_name<< std::endl;
        cons_prod_mutex->lock();
       // std::cout << "count: " << count << std::endl;
      // std::cout << "write after lock " << buffer_mutex_name << std::endl;
        for (int i = 0; i < count; ++i) {
           // std::cout << "*m_prod_index: " << *m_prod_index << " " << buffer_mutex_name <<std::endl;
           // std::cout << "write i " << i << " " << buffer_mutex_name << std::endl;
           // std::cout << "*m_prod_index " << *m_prod_index << " *m_cons_index " << *m_cons_index << " " << buffer_mutex_name << std::endl;
            if ((*m_prod_index + 1) % m_buf_size == *m_cons_index) {
              //  std::cout << "buf is full " << buffer_mutex_name <<std::endl;
                if (! *yet_post_full) {
                  //  std::cout << " cond_full->post(); " << buffer_mutex_name <<std::endl;
                    *yet_post_full = true;
                    cond_full->post();
                }
                *yet_post_empty = false;
                cons_prod_mutex->unlock();
              //  std::cout << "before empty wait " << buffer_mutex_name << std::endl;
                cond_empty->wait();
              //  std::cout << "after empty wait " << buffer_mutex_name << std::endl;
                cons_prod_mutex->lock();
              //  std::cout << "*m_prod_index " << *m_prod_index << " *m_cons_index " << *m_cons_index << " " << buffer_mutex_name << std::endl;

            }
            m_buffer[*m_prod_index] = data[i];
            //std::cout << "elem " << data[i] << " " << buffer_mutex_name << std::endl;
            *m_prod_index = (*m_prod_index + 1 ) % m_buf_size;
        }
        if (! *yet_post_full) {
            //std::cout << " cond_full->post(); " << buffer_mutex_name << std::endl;
            *yet_post_full = true;
            cond_full->post();
        }
        cons_prod_mutex->unlock();
        //std::cout << "writer before notify" << std::endl;
        //std::cout << "*m_prod_index " << *m_prod_index << " *m_cons_index " << *m_cons_index << std::endl;
        m_buffer_mutex->post();
        //std::cout << "writer end " << buffer_mutex_name << std::endl;
    }


};
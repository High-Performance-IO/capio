#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>

using namespace boost::interprocess;
template <class T>
class capio_proxy {
private:
    std::string m_shm_name;
    const std::string m_mode;
    managed_shared_memory m_shm;
    T* m_buffer;
    const int m_buf_size;
    int* m_i_prod;
    int* m_i_cons;
    named_semaphore m_num_stored;
    named_semaphore m_num_empty;
    named_semaphore m_mutex_finished;
    bool* m_finished;
public:
    /*
 * create shared memory
 */
    capio_proxy(const std::string & name, std::string mode, int buf_size = 1024) :
    m_num_stored(open_or_create, ("num_stored_" + name + "_capio_shm").c_str() , 0),
    m_num_empty(open_or_create, ("num_empty_" + name + "_capio_shm").c_str(), buf_size),
    m_mutex_finished(open_or_create, ("mutex_finished" + name + "_capio_shm").c_str(), 1),
    m_buf_size(buf_size), m_mode(mode) {
        m_shm_name = name + "_capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(), 65536); //create only?
        m_buffer = m_shm.find_or_construct<T>("myshmvector")[buf_size]();
        m_i_prod =  m_shm.find_or_construct<int>((m_shm_name + "_m_i_prod").c_str())(0);
        m_i_cons = m_shm.find_or_construct<int>((m_shm_name + "_m_i_cons").c_str())(0);
        m_finished = m_shm.find_or_construct<bool>((m_shm_name + "m_finished").c_str())(false);
        //named_semaphore start_sem(open_or_create_t(), "start_sem", 0);
        //named_semaphore end_sem(open_or_create_t(), "end_sem", 0);

    }


    void clean_resources() {
        m_mutex_finished.post();
        m_shm.destroy_ptr(m_buffer);
        m_shm.destroy_ptr(m_i_prod);
        m_shm.destroy_ptr(m_i_cons);
        named_semaphore::remove(("num_stored_" + m_shm_name).c_str());
        named_semaphore::remove(("num_empty_" + m_shm_name).c_str());
        named_semaphore::remove(("mutex_finished" + m_shm_name).c_str());
        shared_memory_object::remove(m_shm_name.c_str());
    }

/*
 * Puts the object into the shared memory
 */
    bool write(const T& data){
        bool res = false;
        if (m_mode == "producer") {
            m_num_empty.wait();
            m_buffer[*m_i_prod % m_buf_size] = data;
            ++(*m_i_prod);
            m_num_stored.post();
            res = true;
        }
        return res;
    }

/*
 * read the string from the shared memory
 */
    T* read(){
        T* data = nullptr;
        if (m_mode == "consumer") {
            m_mutex_finished.wait();
            if (*m_finished) {
                m_mutex_finished.post();
                if (m_num_stored.try_wait()) {
                    data = new T(m_buffer[*m_i_cons % m_buf_size]);
                    ++(*m_i_cons);
                }
            }
            else {
                m_mutex_finished.post();
                m_num_stored.wait();
                data = new T(m_buffer[*m_i_cons % m_buf_size]);
                ++(*m_i_cons);
                m_num_empty.post();
            }

        }
        return data;
    }

    void finished() {
        m_mutex_finished.wait();
        *m_finished = true;
        m_mutex_finished.post();
    }

    bool done() {
        bool finished;
        m_mutex_finished.wait();
        finished = *m_finished;
        m_mutex_finished.post();
        return finished && (*m_i_prod == *m_i_cons);
    }
};

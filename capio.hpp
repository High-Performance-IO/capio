#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>

using namespace boost::interprocess;
template <class T>
class capio_proxy {
private:
    // name of the segment of shared memory used
    std::string m_shm_name;
    // object to manage the segment of shared memory used
    managed_shared_memory m_shm;
    // circular buffer used to write and read data
    T* m_buffer;
    // buffer size
    const int m_buf_size;
    //  index of the next place of the buffer in which put the new data (for producers). It resides in shared memory
    int* m_i_prod;
    // index of next data to read (for consumers). It resides in shared memory
    int* m_i_cons;
    // semaphore esed to implement critical section during the manipulation of the buffer
    named_semaphore m_mutex_buf;
    // semaphore with value equals to the number of element stored in the circular buffer
    named_semaphore m_num_stored;
    // semaphore with value equals to the number of empty space in the circular buffer
    named_semaphore m_num_empty;
    // semaphore used to implement critical section during the manipulation of the member m_num_consumers
    named_semaphore m_mutex_num_consumers;
    // true if the consumers have finished with the buffer, false otherwise
    int* m_num_consumers;
public:

    /*
     * constructor
     */

    capio_proxy(const std::string & name, int n_consumers, int buf_size = 1024) :
            m_mutex_buf(open_or_create, ("mutex_buf" + name + "_capio_shm").c_str(), 1),
            m_num_stored(open_or_create, ("num_stored_" + name + "_capio_shm").c_str() , 0),
            m_num_empty(open_or_create, ("num_empty_" + name + "_capio_shm").c_str(), buf_size),
            m_mutex_num_consumers(open_or_create, ("mutex_finished" + name + "_capio_shm").c_str(), 1),
            m_buf_size(buf_size) {
        m_shm_name = name + "_capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(), 65536); //create only?
        m_buffer = m_shm.find_or_construct<T>("myshmvector")[buf_size]();
        m_i_prod =  m_shm.find_or_construct<int>((m_shm_name + "_m_i_prod").c_str())(0);
        m_i_cons = m_shm.find_or_construct<int>((m_shm_name + "_m_i_cons").c_str())(0);
        m_num_consumers = m_shm.find_or_construct<int>((m_shm_name + "m_finished").c_str())(n_consumers);
        //named_semaphore start_sem(open_or_create_t(), "start_sem", 0);
        //named_semaphore end_sem(open_or_create_t(), "end_sem", 0);

    }

    /*
     * destructor frees the resources if the consumers have finished and if there aren't element in the buffer
     */

    ~capio_proxy() {
        if (*m_num_consumers == 0 && *m_i_cons == *m_i_prod) {
            m_mutex_num_consumers.post();
            m_shm.destroy_ptr(m_buffer);
            m_shm.destroy_ptr(m_i_prod);
            m_shm.destroy_ptr(m_i_cons);
            named_semaphore::remove(("num_stored_" + m_shm_name).c_str());
            named_semaphore::remove(("num_empty_" + m_shm_name).c_str());
            named_semaphore::remove(("mutex_finished" + m_shm_name).c_str());
            shared_memory_object::remove(m_shm_name.c_str());
        }
    }

    /*
    * Puts the data into the shared memory
    *
    * parameter:
    * const T& data: data to put in shared memory
    *
    * returns NONE
    */
    void write(const T& data){
        m_num_empty.wait();
        m_mutex_buf.wait();
        m_buffer[*m_i_prod % m_buf_size] = data;
        ++(*m_i_prod);
        m_mutex_buf.post();
        m_num_stored.post();

    }

    /*
     * read one unit of data from the shared memory
     *
     * parameters
     * T* data: pointer of the data in producer memory
     *
     * returns
     * true if the data is read, false if there are no active consumers and no data in thr buffer
     * in this second case the data pointed by the parameter is not modified.
     */

    bool read(T* data){
        bool res = false;
        if (! done()) {
            m_num_stored.wait();
            m_mutex_buf.wait();
            *data = m_buffer[*m_i_cons % m_buf_size];
            ++(*m_i_cons);
            m_mutex_buf.post();
            m_num_empty.post();
            res = true;
        }
        return res;
    }

    /*
     * the consumer use this function to informs the it has finished to use the buffer
     */
    void finished() {
        m_mutex_num_consumers.wait();
        --(*m_num_consumers);
        m_mutex_num_consumers.post();
    }

    /*
     * checks if the producer can stop to read the buffer
     *
     * parameters NONE
     *
     * returns
     * true if there are data to read (in the present or in the future), false otherwise
     */

    bool done() {
        bool finished;
        m_mutex_num_consumers.wait();
        finished = *m_num_consumers == 0;
        m_mutex_num_consumers.post();
        return finished && (*m_i_prod == *m_i_cons);
    }
};

#include <string>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>

using namespace boost::interprocess;
template <class T>
class capio_unordered {
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
    // semaphore esed to implement critical section during the manipulation of the buffer for the producers
    named_semaphore m_mutex_buf_prods;
    // semaphore esed to implement critical section during the manipulation of the buffer for the consumers
    named_semaphore m_mutex_buf_cons;
    // semaphore with value equals to the number of element stored in the circular buffer
    named_semaphore m_num_stored;
    // semaphore with value equals to the number of empty space in the circular buffer
    named_semaphore m_num_empty;
    // semaphore used to implement critical section during the manipulation of the member m_num_producer
    named_semaphore m_mutex_num_producers;
    // true if the consumers have finished with the buffer, false otherwise
    int* m_num_producers;
    // true if the process must free the resource when the proxy is destroyed, false otherwise
    bool m_owner;
public:

    /*
     * constructor
     */

    capio_unordered(const std::string & name, int n_producers, bool owner, int buf_size = 1024) :
            m_mutex_buf_prods(open_or_create, ("mutex_buf_prods" + name + "_capio_shm").c_str(), 1),
            m_mutex_buf_cons(open_or_create, ("mutex_buf_cons" + name + "_capio_shm").c_str(), 1),
            m_num_stored(open_or_create, ("num_stored_" + name + "_capio_shm").c_str() , 0),
            m_num_empty(open_or_create, ("num_empty_" + name + "_capio_shm").c_str(), buf_size),
            m_mutex_num_producers(open_or_create, ("mutex_num_producers" + name + "_capio_shm").c_str(), 1),
            m_buf_size(buf_size) {
        m_shm_name = name + "_capio_shm";
        m_shm = managed_shared_memory(open_or_create, m_shm_name.c_str(), 65536); //create only?
        m_buffer = m_shm.find_or_construct<T>("myshmvector")[buf_size]();
        m_i_prod =  m_shm.find_or_construct<int>((m_shm_name + "_m_i_prod").c_str())(0);
        m_i_cons = m_shm.find_or_construct<int>((m_shm_name + "_m_i_cons").c_str())(0);
        m_num_producers = m_shm.find_or_construct<int>((m_shm_name + "m_num_producers").c_str())(n_producers);
        m_owner = owner;
        //named_semaphore start_sem(open_or_create_t(), "start_sem", 0);
        //named_semaphore end_sem(open_or_create_t(), "end_sem", 0);

    }

    /*
     * destructor frees the resources if the producers have finished and if there aren't element in the buffer
     */

    ~capio_unordered() {
        if (*m_num_producers == 0 && *m_i_cons == *m_i_prod && m_owner) {
            std::cout << "a consumer frees the resources" << std::endl;
            m_shm.destroy_ptr(m_num_producers);
            m_shm.destroy_ptr(m_buffer);
            m_shm.destroy_ptr(m_i_prod);
            m_shm.destroy_ptr(m_i_cons);
            named_semaphore::remove(("num_stored_" + m_shm_name).c_str());
            named_semaphore::remove(("num_empty_" + m_shm_name).c_str());
            named_semaphore::remove(("mutex_num_producers" + m_shm_name).c_str());
            named_semaphore::remove(("mutex_buf_prods" + m_shm_name).c_str());
            named_semaphore::remove(("mutex_buf_cons" + m_shm_name).c_str());
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
        m_mutex_buf_prods.wait();
        m_buffer[*m_i_prod % m_buf_size] = data;
        ++(*m_i_prod);
        m_mutex_buf_prods.post();
        m_num_stored.post();

    }

    /*
    * It applies the data transformation and then it
    * puts the data into the shared memory
    *
    * parameter:
    * const T& data: data to put in shared memory
    * const std::function<T(Q)>& func: function to be applied to data
    * returns NONE
    */

    template <class Q>
    void write(const Q& data, const std::function<T(Q)>& func){
        m_num_empty.wait();
        m_mutex_buf_prods.wait();
        m_buffer[*m_i_prod % m_buf_size] = func(data);
        ++(*m_i_prod);
        m_mutex_buf_prods.post();
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
        m_num_stored.wait();
        if (done()) {
            m_num_stored.post(); // avoid that someone is  blocked by m_num_stored if it's call this function again
        }
        else {
            m_mutex_buf_cons.wait();
            *data = m_buffer[*m_i_cons % m_buf_size];
            ++(*m_i_cons);
            m_mutex_buf_cons.post();
            m_num_empty.post();
            res = true;
        }
        return res;
    }

    /*
     * the consumer use this function to informs the it has finished to use the buffer
     *
     * parameters NONE
     *
     * returns NONE
     *
     */

    void finished() {
        m_mutex_num_producers.wait();
        --(*m_num_producers);
        if ((*m_num_producers) == 0) {
            m_num_stored.post(); // avoid that someone is  blocked by m_num_stored if it's call the function read again
        }
        m_mutex_num_producers.post();
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
        m_mutex_num_producers.wait();
        finished = (*m_num_producers) == 0;
        m_mutex_num_producers.post();
        return finished && (*m_i_prod == *m_i_cons);
    }
};

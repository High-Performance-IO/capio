#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <mpi.h>
#include "../capio_ordered/capio_ordered.hpp"

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>
using namespace boost::interprocess;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    if (argc != 2) {
        std::cout << "input error number of consumers needed" << std::endl;
        MPI_Finalize();
        return 1;
    }
    int num_cons = std::stoi(argv[1]);
//capio norm coll
    std::string buffer_mutex_name;
    std::string num_empty_mutex_name;
    std::string num_stored_mutex_name;
    std::string cons_index_memory_name;
    std::string prod_index_memory_name;
    std::string cons_prod_mutex_name;
    std::string cond_empty_name;
    std::string cond_full_name;

    for (int i = 0; i < num_cons; ++i) {
        std::string buffer_mutex_name = ("normmtx_buf_" + std::to_string(i));
        std::string num_empty_mutex_name = ("normmtx_empty_" + std::to_string(i));
        std::string num_stored_mutex_name = ("normmtx_strd_" + std::to_string(i));
        std::string cons_index_memory_name = ("normcons_i_" + std::to_string(i));
        std::string prod_index_memory_name = ("normprod_i_" + std::to_string(i));
        std::string cons_prod_mutex_name = ("normmtx_prdcns_" + std::to_string(i));
        std::string cond_empty_name = ("normcnd_empty_" + std::to_string(i));
        std::string cond_full_name = ("normcnd_full_" + std::to_string(i));
        named_mutex::remove(cons_prod_mutex_name.c_str());
        named_condition::remove(cond_empty_name.c_str());
        named_condition::remove(cond_full_name.c_str());
        named_semaphore::remove(buffer_mutex_name.c_str());
        named_semaphore::remove(num_empty_mutex_name.c_str());
        named_semaphore::remove(num_stored_mutex_name.c_str());
        shared_memory_object::remove(cons_index_memory_name.c_str());
        shared_memory_object::remove(prod_index_memory_name.c_str());

         buffer_mutex_name = ("collmtx_buf_" + std::to_string(i));
         num_empty_mutex_name = ("collmtx_empty_" + std::to_string(i));
         num_stored_mutex_name = ("collmtx_strd_" + std::to_string(i));
         cons_index_memory_name = ("collcons_i_" + std::to_string(i));
         prod_index_memory_name = ("collprod_i_" + std::to_string(i));
        cons_prod_mutex_name = ("collmtx_prdcns_" + std::to_string(i));
        cond_empty_name = ("collcnd_empty_" + std::to_string(i));
        cond_full_name = ("collcnd_full_" + std::to_string(i));
        named_mutex::remove(cons_prod_mutex_name.c_str());
        named_condition::remove(cond_empty_name.c_str());
        named_condition::remove(cond_full_name.c_str());
        named_semaphore::remove(buffer_mutex_name.c_str());
        named_semaphore::remove(num_empty_mutex_name.c_str());
        named_semaphore::remove(num_stored_mutex_name.c_str());
        shared_memory_object::remove(cons_index_memory_name.c_str());
        shared_memory_object::remove(prod_index_memory_name.c_str());
    }
    buffer_mutex_name = ("capiomtx_buf_0");
    num_empty_mutex_name = ("capiomtx_empty_0");
    num_stored_mutex_name = ("capiomtx_strd_0");
    cons_index_memory_name = ("capiocons_i_0");
    prod_index_memory_name = ("capioprod_i_0");
    cons_prod_mutex_name = ("capiomtx_prdcns_0");
    cond_empty_name = ("capiocnd_empty_0");
    cond_full_name = ("capiocnd_full_0");
    named_mutex::remove(cons_prod_mutex_name.c_str());
    named_condition::remove(cond_empty_name.c_str());
    named_condition::remove(cond_full_name.c_str());
    named_semaphore::remove(buffer_mutex_name.c_str());
    named_semaphore::remove(num_empty_mutex_name.c_str());
    named_semaphore::remove(num_stored_mutex_name.c_str());
    shared_memory_object::remove(cons_index_memory_name.c_str());
    shared_memory_object::remove(prod_index_memory_name.c_str());


    shared_memory_object::remove("num_recipients");
    shared_memory_object::remove("num_producers");
    named_semaphore::remove("mutex_num_recipients_capio_shm");
    named_semaphore::remove("mutex_num_prods_capio_shm");
    named_semaphore::remove("sem_num_prods");
    shared_memory_object::remove("capio_shm");
    std::cout << "capio clean ended" << std::endl;
    MPI_Finalize();
    return 0;
}
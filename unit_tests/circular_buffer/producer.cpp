#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <iostream>
#include <string>
#include "common.hpp"
#include <mpi.h>
#include <semaphore.h>
#include "capio/circular_buffer.hpp"

void sync_with_cons(sem_t* sem_prod, sem_t* sem_cons) {
	if (sem_wait(sem_prod) == -1)
		err_exit("sem wait sem_prod");
	if (sem_post(sem_cons) == -1)
		err_exit("sem post sem_cons");
}

void test_one_to_one(const std::string& buffer_name, long int buff_size, long int num_elems, int rank, sem_t* sem_prod, sem_t* sem_cons) {
	Circular_buffer<int> c_buff(buffer_name + std::to_string(rank), buff_size, sizeof(int));
	int val;
	for (long int i = 0; i < num_elems; ++i) {
		val = i % 10 + rank;
		c_buff.write(&val);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	sync_with_cons(sem_prod, sem_cons);
	c_buff.free_shm();
}

void test_one_to_one_str(const std::string& buffer_name, long int buff_size, long int num_elems, int rank, sem_t* sem_prod, sem_t* sem_cons) {
	Circular_buffer<char> c_buff(buffer_name + std::to_string(rank), buff_size, 6 * sizeof(char));
	int val;
	std::string str;
	for (long int i = 0; i < num_elems; ++i) {
		val = i % 10;
		str = "ciao" + std::to_string(val);
		const char* c_str = str.c_str();
		c_buff.write(c_str);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	sync_with_cons(sem_prod, sem_cons);
	c_buff.free_shm();
}


void test_4(int rank, sem_t* sem_prod, sem_t* sem_cons) {
	Circular_buffer<int> c_buff("test_buffer", 1024, sizeof(int));
	int val;
	for (int i = 0; i < 4096; ++i) {
		val = i % 8;
		c_buff.write(&val);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	sync_with_cons(sem_prod, sem_cons);
	c_buff.free_shm();
}

sem_t* get_sem(const std::string sem_name) {
	sem_t* sem = sem_open(sem_name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0); //check the flags
	if (sem == SEM_FAILED)
		err_exit("sem_open " + sem_name);
	return sem;
}

static const std::string sem_prod_name("sem_prod");
static const std::string sem_cons_name("sem_cons");



int main(int argc, char** argv) {
	int rank;
	sem_t* sem_prod;
	sem_t* sem_cons;
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	sem_prod = get_sem(sem_prod_name);
	sem_cons = get_sem(sem_cons_name);
/*
 * simple test that does not fill the buffer.
 * The consumer with rank r communicate with the producer
 * with rank r using an exclusive buffer
 */
	test_one_to_one("test1_buffer", 1024, 16, rank, sem_prod, sem_cons);
	std::cout << "test 1: success!" << std::endl;

/* testing until the buffer is full.
 * The consumer with rank r communicate with the producer
 * with rank r using an exclusive buffer
 */

	test_one_to_one("test2_buffer", 1024, 1024, rank, sem_prod, sem_cons);
	std::cout << "test 2: success!" << std::endl;

/* testing the repeated fulfilling of the buffer
 * The consumer with rank r communicate with the producer
 * with rank r using an exclusive buffer
 */

	test_one_to_one("test3_buffer", 1024, 4096, rank, sem_prod, sem_cons);
	std::cout << "test 3: success!" << std::endl;

	test_4(rank, sem_prod, sem_cons);
	std::cout << "test 4: success!" << std::endl;

	test_one_to_one_str("teststr1_buffer", 1024, 16, rank, sem_prod, sem_cons);
	std::cout << "test 5: success!" << std::endl;

	test_one_to_one_str("teststr2_buffer", 16* 6, 16, rank, sem_prod, sem_cons);
	std::cout << "test 6: success!" << std::endl;

	test_one_to_one_str("teststr3_buffer", 1024, 4096, rank, sem_prod, sem_cons);
	std::cout << "test 7: success!" << std::endl;

	sem_close(sem_prod);
	sem_close(sem_cons);
	sem_unlink(sem_prod_name.c_str());
	sem_unlink(sem_cons_name.c_str());

	MPI_Finalize();

	return 0;
}

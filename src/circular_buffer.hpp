#ifndef CAPIO_CIRCULAR_BUFFER_HPP_
#define CAPIO_CIRCULAR_BUFFER_HPP_

#include <string>
#include <iostream>

#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

/*
 * Multi-producer and multi-consumer circular buffer.
 * Each element of the circular buffer has the same size.
 */
template <class T>
class Circular_buffer {
	private:
		void* _shm;
		const long int _max_num_elems;
		const long int _elem_size; //elements size in bytes
		long int _buff_size; // buffer size in bytes
		long int _first_elem;
		long int _last_elem;
		const std::string _shm_name;
		sem_t* _mutex;
		sem_t* _sem_num_elems;
		sem_t* _sem_num_empty;

		void err_exit(std::string error_msg) {
			std::cerr << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
			exit(1);
		}
		
		void* create_shm(std::string shm_name, const long int size) {
			void* p = nullptr;
			// if we are not creating a new object, mode is equals to 0
			int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
			if (fd == -1)
				err_exit("shm_open " + shm_name);
			if (ftruncate(fd, size * _elem_size) == -1)
				err_exit("ftruncate " + shm_name);
			p = mmap(NULL, size * _elem_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
			if (p == MAP_FAILED)
				err_exit("mmap " + shm_name);

			if (close(fd) == -1)
				err_exit("close " + shm_name);
		return p;
	}	

	public:
		Circular_buffer(const std::string shm_name, const long int _max_num_elems, const long int elem_size) : _max_num_elems(_max_num_elems), _elem_size(elem_size), _shm_name(shm_name) {
			_shm = create_shm(shm_name, _max_num_elems);	
			_buff_size = _max_num_elems * _elem_size;
			_first_elem = 0;
			_last_elem = 0;
			_mutex = sem_open(("_mutex" + _shm_name).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 1); //check the flags
			if (_mutex == SEM_FAILED) {
				err_exit("sem_open _mutex" + _shm_name);
			}
			_sem_num_elems = sem_open(("_sem_num_elems" + _shm_name).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0); //check the flags
			if (_sem_num_elems == SEM_FAILED) {
				err_exit("sem_open _sem_num_elems" + _shm_name);
			}
			_sem_num_empty = sem_open(("_sem_num_empty" + _shm_name).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, _max_num_elems); //check the flags
			if (_sem_num_empty == SEM_FAILED) {
				err_exit("sem_open _sem_num_empty" + _shm_name);
			}
		}

		~Circular_buffer() {
			shm_unlink(_shm_name.c_str());
			sem_close(_mutex);
			sem_close(_sem_num_elems);
			sem_close(_sem_num_empty);
		}

		void free_shm() {
			sem_unlink(("_mutex" + _shm_name).c_str());
			sem_unlink(("_sem_num_elems" + _shm_name).c_str());
			sem_unlink(("_sem_num_empty" + _shm_name).c_str());
		}
	
		void write(T* data) {
			if (sem_wait(_sem_num_empty) == -1)
				err_exit("sem_wait _sem_num_empty");
			
			if (sem_wait(_mutex) == -1)
				err_exit("sem_wait _mutex in write");
			
			memcpy(_shm + _last_elem, data, _elem_size);
			_last_elem = (_last_elem + _elem_size) % _buff_size;

			if (sem_post(_mutex) == -1)
				err_exit("sem_post _mutex in write");

			if (sem_post(_sem_num_elems) == -1)
				err_exit("sem_post _sem_num_elems");
		}

		void read(T* buff_rcv) {
			int res;
			if (sem_wait(_sem_num_elems) == -1)
				err_exit("sem_wait _sem_num_elems");

			if (sem_wait(_mutex) == -1)
				err_exit("sem_wait _mutex in write");

			memcpy(buff_rcv, _shm + _first_elem, _elem_size);
			_first_elem = (_first_elem + _elem_size) % _buff_size;

			if (sem_post(_mutex) == -1)
				err_exit("sem_wait _mutex in write");

			if (sem_post(_sem_num_empty) == -1)
				err_exit("sem_post _sem_num_empty");
		}
};

#endif

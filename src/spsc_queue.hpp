#ifndef CAPIO_SPSC_CIRCULAR_BUFFER_HPP_
#define CAPIO_SPSC_CIRCULAR_BUFFER_HPP_

#include <string>
#include <iostream>

#include <cstring>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <climits>

/*
 * Multi-producer and multi-consumer circular buffer.
 * Each element of the circular buffer has the same size.
 */

template <class T>
class SPSC_queue {
	private:
		void* _shm;
		const long int _max_num_elems;
		const long int _elem_size; //elements size in bytes
		long int _buff_size; // buffer size in bytes
		long int* _first_elem;
		long int* _last_elem;
		const std::string _shm_name;
		sem_t* _sem_num_elems;
		sem_t* _sem_num_empty;

		void err_exit(const std::string& error_msg) {
			std::cerr << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
			exit(1);
		}
		
		void* create_shm(const std::string& shm_name, const long int size) {
			void* p;
			// if we are not creating a new object, mode is equals to 0
			int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
			if (fd == -1)
				err_exit("shm_open " + shm_name);
			if (ftruncate(fd, size) == -1)
				err_exit("ftruncate " + shm_name);
			p = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
			if (p == MAP_FAILED)
				err_exit("mmap " + shm_name);

			if (close(fd) == -1)
				err_exit("close " + shm_name);
		return p;
	}	

	int get_shm() {
	// if we are not creating a new object, mode is equals to 0
		int fd = shm_open(_shm_name.c_str(), O_RDWR, 0); //to be closed
		struct stat sb;
		if (fd == -1)
			return -1;
		/* Open existing object */
		/* Use shared memory object size as length argument for mmap()
		and as number of bytes to write() */
		if (fstat(fd, &sb) == -1)
			err_exit("fstat " + _shm_name);
		_shm = mmap(nullptr, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (_shm == MAP_FAILED)
			err_exit("mmap " + _shm_name);
		if (close(fd) == -1)
			err_exit("close");

		return 0;
	}
	public:
		SPSC_queue(const std::string shm_name, const long int _max_num_elems, const long int elem_size) : _max_num_elems(_max_num_elems), _elem_size(elem_size), _shm_name(shm_name) {
			_buff_size = _max_num_elems * _elem_size;
			_first_elem = (long int*) create_shm("_first_elem" + shm_name, sizeof(long int));
			_last_elem = (long int*) create_shm("_last_elem" + shm_name, sizeof(long int));
			if (get_shm() == -1) {
				*_first_elem = 0;
				*_last_elem = 0;
				_shm = create_shm(shm_name, _buff_size);	
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

		~SPSC_queue() {
			sem_close(_sem_num_elems);
			sem_close(_sem_num_empty);
		}

		void free_shm() {
			shm_unlink(_shm_name.c_str());
			shm_unlink(("_first_elem" + _shm_name).c_str());
			shm_unlink(("_last_elem" + _shm_name).c_str());
			sem_unlink(("_sem_num_elems" + _shm_name).c_str());
			sem_unlink(("_sem_num_empty" + _shm_name).c_str());
		}
	
		void write(const T* data) {
			if (sem_wait(_sem_num_empty) == -1)
				err_exit("sem_wait _sem_num_empty");
			
			memcpy((char*) _shm + *_last_elem, data, _elem_size);
			*_last_elem = (*_last_elem + _elem_size) % _buff_size;

			if (sem_post(_sem_num_elems) == -1)
				err_exit("sem_post _sem_num_elems");
		}
			
		void write(const T* data, long int num_bytes) {

			if (num_bytes > _elem_size) {
				std::cerr << "circular buffer " + _shm_name + "write error: num_bytes > _elem_size"  << std::endl;
				exit(1);
			}

			if (sem_wait(_sem_num_empty) == -1)
				err_exit("sem_wait _sem_num_empty");
				
			memcpy((char*) _shm + *_last_elem, data, num_bytes);
			*_last_elem = (*_last_elem + _elem_size) % _buff_size;

			if (sem_post(_sem_num_elems) == -1)
				err_exit("sem_post _sem_num_elems");
		}

		void read(T* buff_rcv) {
			if (sem_wait(_sem_num_elems) == -1)
				err_exit("sem_wait _sem_num_elems");


			memcpy((char*) buff_rcv, ((char*) _shm) + *_first_elem, _elem_size);
			*_first_elem = (*_first_elem + _elem_size) % _buff_size;


			if (sem_post(_sem_num_empty) == -1)
				err_exit("sem_post _sem_num_empty");
		}

		/* 
		 * It reads only the firsts num_bytes bytes of the buffer element 
		 */

		void read(T* buff_rcv, long int num_bytes) {
			if (num_bytes > _elem_size) {
				std::cerr << "circular buffer " + _shm_name + "read error: num_bytes > _elem_size"  << std::endl;
				exit(1);
			}

			if (sem_wait(_sem_num_elems) == -1)
				err_exit("sem_wait _sem_num_elems");


			memcpy((char*) buff_rcv, ((char*) _shm) + *_first_elem, num_bytes);
			*_first_elem = (*_first_elem + _elem_size) % _buff_size;

			if (sem_post(_sem_num_empty) == -1)
				err_exit("sem_post _sem_num_empty");
		}
		
};

#endif

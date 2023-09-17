#ifndef CAPIO_COMMON_CIRCULAR_BUFFER_HPP
#define CAPIO_COMMON_CIRCULAR_BUFFER_HPP

# include <semaphore.h>

#include "capio/errors.hpp"
#include "capio/shm.hpp"

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
		long int* _first_elem;
		long int* _last_elem;
		const std::string _shm_name;
		sem_t* _mutex;
		sem_t* _sem_num_elems;
		sem_t* _sem_num_empty;

	public:
		Circular_buffer(const std::string& shm_name, const long int _max_num_elems, const long int elem_size) : _max_num_elems(_max_num_elems), _elem_size(elem_size), _shm_name(shm_name) {
			_buff_size = _max_num_elems * _elem_size;
			_first_elem = (long int*) create_shm("_first_elem" + shm_name, sizeof(long int));
			_last_elem = (long int*) create_shm("_last_elem" + shm_name, sizeof(long int));
			_shm = get_shm_if_exist(_shm_name);
                        if (_shm == nullptr) {
				*_first_elem = 0;
				*_last_elem = 0;
				_shm = create_shm(shm_name, _buff_size);	
			}
			
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
			sem_close(_mutex);
			sem_close(_sem_num_elems);
			sem_close(_sem_num_empty);
		}

		void free_shm(std::ostream& outstream = std::cerr) {
			if (shm_unlink(_shm_name.c_str()) == -1)
				err_exit("shm_unlink " + _shm_name + " in circular_buffer free_shm");
			std::string resource = "_first_elem" + _shm_name;
			if (shm_unlink(resource.c_str()) == -1)
				err_exit("shm_unlink " + resource + " in circular_buffer free_shm");
			resource = "_last_elem" + _shm_name;
			if (shm_unlink(resource.c_str()) == -1)
				err_exit("shm_unlink " + resource + " in circular_buffer free_shm");
			resource = "_mutex" + _shm_name;
			if (sem_unlink(resource.c_str()) == -1)
				err_exit("sem_unlink " + resource + " in circular_buffer free_shm");
			resource = "_sem_num_elems" + _shm_name;
			if (sem_unlink(resource.c_str()) == -1)
				err_exit("sem_unlink " + resource + " in circular_buffer free_shm");
			resource = "_sem_num_empty" + _shm_name;
			if (sem_unlink(resource.c_str()) == -1)
				err_exit("sem_unlink " + resource + " in circular_buffer free_shm");
		}
	
		void write(const T* data) {
			if (sem_wait(_sem_num_empty) == -1)
				err_exit("sem_wait _sem_num_empty");
			
			if (sem_wait(_mutex) == -1)
				err_exit("sem_wait _mutex in write");
			
			memcpy((char*) _shm + *_last_elem, data, _elem_size);
			*_last_elem = (*_last_elem + _elem_size) % _buff_size;

			if (sem_post(_mutex) == -1)
				err_exit("sem_post _mutex in write");

			if (sem_post(_sem_num_elems) == -1)
				err_exit("sem_post _sem_num_elems");
		}
			
		void write(const T* data, long int num_bytes) {
			if (sem_wait(_sem_num_empty) == -1)
				err_exit("sem_wait _sem_num_empty");
			
			if (sem_wait(_mutex) == -1)
				err_exit("sem_wait _mutex in write");
		
			if (*_last_elem + num_bytes > _buff_size) {
				std::cout << "_last_elem " << *_last_elem << std::endl;
				std::cout << "num_bytes" << num_bytes << std::endl;
				std::cout << "buff_size" << _buff_size << std::endl;
				std::cout << "out of bound write" << std::endl;
			}

			memcpy((char*) _shm + *_last_elem, data, num_bytes);
			//TODO: dangerous consider remove this function
			*_last_elem = (*_last_elem + _elem_size) % _buff_size;

			if (sem_post(_mutex) == -1)
				err_exit("sem_post _mutex in write");

			if (sem_post(_sem_num_elems) == -1)
				err_exit("sem_post _sem_num_elems");
		}

		void read(T* buff_rcv) {
			if (sem_wait(_sem_num_elems) == -1)
				err_exit("sem_wait _sem_num_elems");

			if (sem_wait(_mutex) == -1)
				err_exit("sem_wait _mutex in write");
			memcpy((char*) buff_rcv, ((char*) _shm) + *_first_elem, _elem_size);
			*_first_elem = (*_first_elem + _elem_size) % _buff_size;

			if (sem_post(_mutex) == -1)
				err_exit("sem_wait _mutex in write");

			if (sem_post(_sem_num_empty) == -1)
				err_exit("sem_post _sem_num_empty");
		}
		void read(T* buff_rcv, long int num_bytes) {
			if (num_bytes > _elem_size) {
				std::cerr << "circular buffer " + _shm_name + "read error: num_bytes > _elem_size"  << std::endl;
				exit(1);
			}

			if (sem_wait(_sem_num_elems) == -1)
				err_exit("sem_wait _sem_num_elems");


			memcpy((char*) buff_rcv, ((char*) _shm) + *_first_elem, num_bytes);
			*_first_elem = (*_first_elem + _elem_size) % _buff_size;
			if (sem_post(_mutex) == -1)
				err_exit("sem_post _mutex in write");

			if (sem_post(_sem_num_empty) == -1)
				err_exit("sem_post _sem_num_empty");
		}
		
};

#endif // CAPIO_COMMON_CIRCULAR_BUFFER_HPP

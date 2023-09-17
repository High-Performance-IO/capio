#ifndef CAPIO_COMMON_SPSC_QUEUE_HPP
#define CAPIO_COMMON_SPSC_QUEUE_HPP

#include <semaphore.h>

#include "capio/errors.hpp"
#include "capio/shm.hpp"

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

public:
  SPSC_queue(const std::string& shm_name, const long int _max_num_elems, const long int elem_size) : _max_num_elems(_max_num_elems), _elem_size(elem_size), _shm_name(shm_name) {
    _buff_size = _max_num_elems * _elem_size;
    _first_elem = (long int*) create_shm("_first_elem" + shm_name, sizeof(long int));
    _last_elem = (long int*) create_shm("_last_elem" + shm_name, sizeof(long int));
    _shm = get_shm_if_exist(_shm_name);
    if (_shm == nullptr) {
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

#endif // CAPIO_COMMON_SPSC_QUEUE_HPP

#ifndef CAPIO_COMMON_SHM_HPP
#define CAPIO_COMMON_SHM_HPP

#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errors.hpp"

void* create_shm(const std::string& shm_name, const long int size) {
  void* p;
  // if we are not creating a new object, mode is equals to 0
  int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
  if (fd == -1)
    err_exit("create_shm shm_open " + shm_name, "create_shm" );
  if (ftruncate(fd, size) == -1)
    err_exit("ftruncate create_shm " + shm_name, "create_shm");
  p = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err_exit("mmap create_shm " + shm_name, "create_shm");
  if (close(fd) == -1)
    err_exit("close", "create_shm");
  return p;
}

void* create_shm(const std::string& shm_name, const long int size, int* fd) {
  void* p;
  // if we are not creating a new object, mode is equals to 0
  *fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR); //to be closed
  if (*fd == -1)
    err_exit("create_shm 3 args shm_open " + shm_name, "create_shm" );
  if (ftruncate(*fd, size) == -1)
    err_exit("ftruncate create shm *fd " + shm_name, "create_shm" );
  p = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, 0);
  if (p == MAP_FAILED)
    err_exit("mmap create_shm " + shm_name, "create_shm" );
  return p;
}

void* expand_shared_mem(const std::string& shm_name, long int new_size) {
  void* p;
  // if we are not creating a new object, mode is equals to 0
  int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
  struct stat sb{};
  if (fd == -1) {
    if (errno == ENOENT)
      return nullptr;
    err_exit("get_shm shm_open " + shm_name, "expand_shared_mem" );
  }
  /* Open existing object */
  /* Use shared memory object size as length argument for mmap()
  and as number of bytes to write() */
  if (fstat(fd, &sb) == -1)
    err_exit("fstat " + shm_name, "expand_shared_mem" );
  if (ftruncate(fd, new_size) == -1)
    err_exit("ftruncate expand_shared_mem" + shm_name, "expand_shared_mem" );
  p = mmap(nullptr, new_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err_exit("mmap create_shm " + shm_name, "expand_shared_mem" );
  if (close(fd) == -1)
    err_exit("close", "expand_shared_mem" );
  return p;
}

void* get_shm(const std::string& shm_name) {
  void* p;
  // if we are not creating a new object, mode is equals to 0
  int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
  struct stat sb{};
  if (fd == -1)
    err_exit("get_shm shm_open " + shm_name, "get_shm");
  /* Open existing object */
  /* Use shared memory object size as length argument for mmap()
  and as number of bytes to write() */
  if (fstat(fd, &sb) == -1)
    err_exit("fstat " + shm_name, "get_shm");
  p = mmap(nullptr, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err_exit("mmap get_shm " + shm_name, "get_shm");
  if (close(fd) == -1)
    err_exit("close", "get_shm");
  return p;
}

void* get_shm_if_exist(const std::string& shm_name) {
  void* p;
  // if we are not creating a new object, mode is equals to 0
  int fd = shm_open(shm_name.c_str(), O_RDWR, 0); //to be closed
  struct stat sb{};
  if (fd == -1) {
    if (errno == ENOENT)
      return nullptr;
    err_exit("get_shm shm_open " + shm_name, "get_shm_if_exist");
  }
  /* Open existing object */
  /* Use shared memory object size as length argument for mmap()
  and as number of bytes to write() */
  if (fstat(fd, &sb) == -1)
    err_exit("fstat " + shm_name, "get_shm_if_exist");
  p = mmap(nullptr, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    err_exit("mmap get_shm " + shm_name, "get_shm_if_exist");
  if (close(fd) == -1)
    err_exit("close", "get_shm_if_exist");
  return p;
}

#endif // CAPIO_COMMON_SHM_HPP

#ifndef CAPIO_POSIX_UTILS_SHM_HPP
#define CAPIO_POSIX_UTILS_SHM_HPP

#include "capio/constants.hpp"
#include "capio/shm.hpp"
#include "capio/spsc_queue.hpp"

#include "logger.hpp"

void read_shm(SPSC_queue<char> *data_buf, long int offset, void *buffer, off64_t count) {
  size_t n_reads = count / WINDOW_DATA_BUFS;
  size_t r = count % WINDOW_DATA_BUFS;
#ifdef CAPIOLOG
  CAPIO_DBG("read shm %ld %ld\n", n_reads, r);
  if (r)
    CAPIO_DBG("lol shm %ld %ld\n", n_reads, r);
#endif
  size_t i = 0;
  while (i < n_reads) {
    data_buf->read((char *) buffer + i * WINDOW_DATA_BUFS);
    ++i;
  }
  if (r)
    data_buf->read((char *) buffer + i * WINDOW_DATA_BUFS, r);

}

void write_shm(SPSC_queue<char> *data_buf, size_t offset, const void *buffer, off64_t count) {

  size_t n_writes = count / WINDOW_DATA_BUFS;
  size_t r = count % WINDOW_DATA_BUFS;
#ifdef CAPIOLOG
  CAPIO_DBG("write shm %ld %ld\n", n_writes, r);
  if (r)
    CAPIO_DBG("lol shm %ld %ld\n", n_writes, r);
#endif
  size_t i = 0;
  while (i < n_writes) {
    data_buf->write((char *) buffer + i * WINDOW_DATA_BUFS);
    ++i;
  }
  if (r) {

    CAPIO_DBG("write shm debug before\n");

    data_buf->write((char *) buffer + i * WINDOW_DATA_BUFS, r);

    CAPIO_DBG("write shm debug after\n");

  }

}

#endif // CAPIO_POSIX_UTILS_SHM_HPP

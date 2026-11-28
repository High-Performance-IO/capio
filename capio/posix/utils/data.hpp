#ifndef CAPIO_POSIX_UTILS_DATA_HPP
#define CAPIO_POSIX_UTILS_DATA_HPP

#include "cache.hpp"

/**
 * Add a new response buffer for thread @param tid
 * @param tid
 * @return
 */
inline void initialize_data_queues(const long tid) {
    write_cache =
        new WriteCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
    read_cache =
        new ReadCache(tid, get_cache_lines(), get_cache_line_size(), get_capio_workflow_name());
    FS_MODE({
        consent_request_cache = new ConsentRequestCache();
        read_request_cache    = new ReadCacheFS();
    })
}

/**
 * Read up to @count bytes of the CAPIO descriptor @fd, at its file offset, through the read cache:
 * the data of a file for read, or the entries of a directory for getdents. The data this thread
 * wrote must reach the server first, so the write cache is flushed.
 *
 * @param fd
 * @param buffer
 * @param count
 * @param is_getdents whether to read entries of a directory, in whole records
 * @param is64bit for getdents: whether the records are linux_dirent64 (getdents64)
 * @return the number of bytes read, 0 at the end of the file
 */
inline off64_t capio_cached_read(const int fd, void *buffer, const off64_t count,
                                 const bool is_getdents = false, const bool is64bit = false) {
    START_LOG(capio_tid(), "call(fd=%d, count=%ld, is_getdents=%s, is64bit=%s)", fd, count,
              is_getdents ? "true" : "false", is64bit ? "true" : "false");
    if (count >= SSIZE_MAX) {
        ERR_EXIT("CAPIO does not support read bigger than SSIZE_MAX yet");
    }
    write_cache->flush();
    return read_cache->read(fd, buffer, count, is_getdents, is64bit);
}

/**
 * Write @count bytes to the CAPIO descriptor @fd, at its file offset, through the write cache.
 * The read cache is flushed if it holds data of the same file, which the write makes stale.
 *
 * @param fd
 * @param buffer
 * @param count
 */
inline void capio_cached_write(const int fd, const void *buffer, const off64_t count) {
    if (read_cache->cachesFileOf(fd)) {
        read_cache->flush();
    }
    write_cache->write(fd, buffer, count);
}

/**
 * Move the file offset of the CAPIO descriptor @fd to @offset, on the client and on the server,
 * once the caches have given the server the data and offsets it does not know yet.
 *
 * @param fd
 * @param offset
 * @param tid
 */
inline void capio_set_fd_offset(const int fd, const off64_t offset, const long tid) {
    read_cache->flush();
    write_cache->flush();
    fd_table->setOffset(fd, offset);
    seek_request(fd, offset, tid);
}

#endif // CAPIO_POSIX_UTILS_DATA_HPP

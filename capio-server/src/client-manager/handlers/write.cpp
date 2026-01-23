#include <capio/logger.hpp>
#include <capiocl.hpp>
#include <climits>
#include <include/storage-service/capio_storage_service.hpp>

extern CapioStorageService *storage_service;
void write_handler(const char *const str) {
    pid_t tid;
    int fd;
    capio_off64_t write_size;

    char path[PATH_MAX];
    sscanf(str, "%d %d %s %llu", &tid, &fd, path, &write_size);
    START_LOG(gettid(), "call(tid=%d, fd=%d, path=%s, count=%llu)", tid, fd, path, write_size);

    /**
     * File needs to be handled, however, to not overload the client manager thread, on which
     * this function is being called, the function checkAndUnlockThreadAwaitingData is not
     * being called, as worst case scenario after 300ms the FSMonitor thread will catch the
     * occurrence of the write and handle it. TODO: possibly find a low overhead way to improve on
     * this
     */
    LOG("File needs to be handled");
    // file_manager->checkAndUnlockThreadAwaitingData(path);
}

void write_mem_handler(const char *const str) {
    long int tid;
    char path[PATH_MAX];
    off64_t write_size;
    capio_off64_t offset;
    sscanf(str, "%ld %s %llu %ld", &tid, path, &offset, &write_size);
    START_LOG(gettid(), "call(tid=%d, path=%s, offset=%lld, write_size=%lld)", tid, path, offset,
              write_size);

    storage_service->recive_from_client(tid, path, offset, write_size);
}

#include <capio/logger.hpp>
#include <capiocl.hpp>
#include <climits>
#include <include/storage-service/capio_storage_service.hpp>

extern CapioStorageService *storage_service;

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

#include <capio/logger.hpp>
#include <climits>
#include <capiocl.hpp>
#include <include/storage-service/capio_storage_service.hpp>
#include <include/client-manager/client_manager.hpp>
extern ClientManager *client_manager;
extern CapioStorageService *storage_service;
void files_to_store_in_memory_handler(const char *const str) {
    // TODO: register files open for each tid ti register a close
    pid_t tid;
    sscanf(str, "%d", &tid);
    START_LOG(gettid(), "call(tid=%d)", tid);

    auto count = storage_service->sendFilesToStoreInMemory(tid);

    LOG("Need to tell client to read %llu files from data queue", count);
    client_manager->reply_to_client(tid, count);
}
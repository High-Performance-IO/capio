#ifndef CAPIO_CACHE_HPP
#define CAPIO_CACHE_HPP
#include "capio/requests.hpp"
#include "env.hpp"

#include "cache/consent_request_cache.hpp"
#include "cache/read_request_cache_fs.hpp"
#include "cache/read_request_cache_mem.hpp"
#include "cache/write_request_cache_fs.hpp"
#include "cache/write_request_cache_mem.hpp"

inline thread_local ConsentRequestCache *consent_request_cache_fs;
inline thread_local ReadRequestCacheFS *read_request_cache_fs;
inline thread_local WriteRequestCacheFS *write_request_cache_fs;
inline thread_local WriteRequestCacheMEM *write_request_cache_mem;
inline thread_local ReadRequestCacheMEM *read_request_cache_mem;

inline void init_caches() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    write_request_cache_fs   = new WriteRequestCacheFS();
    read_request_cache_fs    = new ReadRequestCacheFS();
    consent_request_cache_fs = new ConsentRequestCache();
    write_request_cache_mem  = new WriteRequestCacheMEM();
    read_request_cache_mem   = new ReadRequestCacheMEM();
}

inline void delete_caches() {
    START_LOG(capio_syscall(SYS_gettid), "call()");
    delete write_request_cache_fs;
    delete read_request_cache_fs;
    delete consent_request_cache_fs;
    delete write_request_cache_mem;
    delete read_request_cache_mem;

    delete cts_queue;
    LOG("Removed cts_queue");
    delete stc_queue;
    LOG("Removed stc_queue");
}

#endif // CAPIO_CACHE_HPP
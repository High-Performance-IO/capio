#ifndef CAPIOCACHESPSCQUEUETESTS_HPP
#define CAPIOCACHESPSCQUEUETESTS_HPP

#include "../posix/utils/env.hpp"
#include "../posix/utils/filesystem.hpp"
#include "../posix/utils/types.hpp"

inline SPSCQueue *cts_queue, *stc_queue;
inline auto bufs_response = new CPBufResponse_t;
inline thread_local auto *buf_requests =
    new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);

#include "../posix/utils/cache.hpp"
#include "SourceText.hpp"

inline WriteRequestCacheMEM writeCache;

void miniServerInstance() {
    cts_queue    = new SPSCQueue("queue-tests.cts", get_cache_lines(), get_cache_line_size());
    auto tmp_buf = new char[4 * 1024];
    cts_queue->read(tmp_buf, 1024);
    EXPECT_TRUE(strncmp(tmp_buf, SOURCE_TEST_TEXT, 1024) == 0);
}

TEST(CapioCacheSPSCQueue, TestCacheWithSpscQueueWrite) {
    std::thread server_thread(miniServerInstance);
    cts_queue = new SPSCQueue("queue-tests.cts", get_cache_lines(), get_cache_line_size());

    writeCache.write(0, SOURCE_TEST_TEXT, 1024);
    writeCache.flush();

    server_thread.join();
}

#endif // CAPIOCACHESPSCQUEUETESTS_HPP

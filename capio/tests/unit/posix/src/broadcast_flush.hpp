#ifndef CAPIO_TEST_UNIT_POSIX_BROADCAST_FLUSH_HPP
#define CAPIO_TEST_UNIT_POSIX_BROADCAST_FLUSH_HPP

#include <gtest/gtest.h>

#include <fcntl.h>

// Expose internal cache state so the test can reconstruct the pending-buffer scenario
// without adding production API.
#define private public
#include "utils/cache.hpp"
#undef private

#include "utils/data.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

#include "handlers/close.hpp"

class ReadCacheCloseTest : public testing::Test {
  protected:
    static void SetUpTestSuite() { init_filesystem(); }

    static void TearDownTestSuite() {
        delete read_cache;
        delete write_cache;
        delete buf_requests;
        delete buff_response;
        destroy_filesystem();
    }

    // Reproduce the per-thread client state: request buffers plus the caches, and a read
    // cache that holds a pending, not-yet-flushed buffer on fd. Each test uses its own
    // tid so the shared-memory buffer names do not collide.
    static int PreparePendingReadCache(const long tid) {
        constexpr int fd = 2;
        init_client(tid);
        initialize_data_queues(tid);
        add_capio_fd(tid, "/capio/file_0.dat", fd, 0, 0, O_RDONLY, false);
        read_cache->_last_fd      = fd;
        read_cache->_cache_offset = 100;
        read_cache->_actual_size  = 0;
        // The pending flush() issues a seek_request(); supply the reply it awaits.
        constexpr off64_t reply   = 0;
        buff_response->write(&reply);
        return fd;
    }

    static bool ReadCacheHasPendingData(ReadCache &cache) {
        return cache._actual_size != cache._cache_offset;
    }
};

// A read cache holding pending data on an fd that is then closed must have that data
// flushed during close, so nothing survives to point at the removed descriptor.
TEST_F(ReadCacheCloseTest, CloseFlushesPendingReadDataBeforeDeletingFd) {
    const int fd = PreparePendingReadCache(/*tid*/ 1);
    long result  = 0;

    EXPECT_NO_THROW(close_handler(fd, 0, 0, 0, 0, 0, &result));
    EXPECT_FALSE(exists_capio_fd(fd));
    EXPECT_FALSE(ReadCacheHasPendingData(*read_cache));

    // A later teardown flush is then a no-op on the closed fd.
    EXPECT_NO_THROW(read_cache->flush());
}

#endif // CAPIO_TEST_UNIT_POSIX_BROADCAST_FLUSH_HPP
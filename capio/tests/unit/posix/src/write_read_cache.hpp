#ifndef CAPIO_TEST_UNIT_POSIX_WRITE_READ_CACHE_HPP
#define CAPIO_TEST_UNIT_POSIX_WRITE_READ_CACHE_HPP

#include <gtest/gtest.h>

#include <fcntl.h>

// Expose internal cache state so the test can reconstruct a read cache holding prefetched data
// without adding production API.
#define private public
#include "utils/cache.hpp"
#undef private

#include "utils/data.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"

#include "handlers/write.hpp"

/*
 * A process that reads one file and writes another (a pipeline stage) alternates read() and
 * write(). The read cache prefetches a whole line of the input; a write to the output cannot make
 * those bytes stale, so it must not drop them: dropping them costs a seek request now and a new
 * line fetch at the next read, for every write.
 */
class WriteReadCacheTest : public testing::Test {
  protected:
    static constexpr int INPUT_FD  = 3;
    static constexpr int OUTPUT_FD = 4;

    static void SetUpTestSuite() { init_filesystem(); }

    // The descriptor table outlives a test: drop this test's descriptors so the next one can
    // bind the same numbers to other files.
    void TearDown() override {
        for (const int fd : {INPUT_FD, OUTPUT_FD}) {
            if (exists_capio_fd(fd)) {
                delete_capio_fd(fd);
            }
        }
    }

    static void TearDownTestSuite() {
        delete read_cache;
        delete write_cache;
        delete buf_requests;
        delete buff_response;
        destroy_filesystem();
    }

    // Client state of a thread whose read cache holds prefetched, not yet consumed bytes of
    // input_path on INPUT_FD. Each test uses its own tid so the shared-memory buffer names do not
    // collide.
    static void PrepareReadCacheWithPrefetchedInput(const long tid, const char *input_path) {
        init_client(tid);
        initialize_data_queues(tid);
        add_capio_fd(tid, input_path, INPUT_FD, 0, 0, O_RDONLY, false);
        read_cache->_last_fd      = INPUT_FD;
        read_cache->_cache_offset = 8192;
        read_cache->_actual_size  = 256 * 1024;
        // If the cache is flushed, flush() issues a seek_request(): supply the reply it awaits
        // so the test cannot block without a server.
        constexpr off64_t reply   = 0;
        buff_response->write(&reply);
    }

    static bool ReadCacheHasPrefetchedData() {
        return read_cache->_actual_size != read_cache->_cache_offset;
    }
};

TEST_F(WriteReadCacheTest, WriteToAnotherFileKeepsPrefetchedReadData) {
    PrepareReadCacheWithPrefetchedInput(/*tid*/ 11, "/capio/p0_0.dat");
    add_capio_fd(11, "/capio/p1_0.dat", OUTPUT_FD, 0, 0, O_WRONLY, false);

    const char data[8] = "payload";
    EXPECT_EQ(capio_write(OUTPUT_FD, data, sizeof(data), 11), static_cast<off64_t>(sizeof(data)));

    EXPECT_TRUE(ReadCacheHasPrefetchedData())
        << "a write to p1_0.dat dropped the prefetched bytes of p0_0.dat";
    EXPECT_EQ(read_cache->_cache_offset, 8192);
}

TEST_F(WriteReadCacheTest, WriteToTheCachedFileDropsPrefetchedReadData) {
    PrepareReadCacheWithPrefetchedInput(/*tid*/ 12, "/capio/inout_0.dat");
    // Another descriptor of the same file (as after dup() or a second open()).
    add_capio_fd(12, "/capio/inout_0.dat", OUTPUT_FD, 0, 0, O_WRONLY, false);

    const char data[8] = "payload";
    EXPECT_EQ(capio_write(OUTPUT_FD, data, sizeof(data), 12), static_cast<off64_t>(sizeof(data)));

    // The write may change bytes the cache holds: they must not be served afterwards.
    EXPECT_FALSE(ReadCacheHasPrefetchedData());
}

#endif // CAPIO_TEST_UNIT_POSIX_WRITE_READ_CACHE_HPP

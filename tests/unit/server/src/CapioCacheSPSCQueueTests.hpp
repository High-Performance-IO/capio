#ifndef CAPIOCACHESPSCQUEUETESTS_HPP
#define CAPIOCACHESPSCQUEUETESTS_HPP

#include "../posix/utils/env.hpp"
#include "../posix/utils/filesystem.hpp"
#include "../posix/utils/types.hpp"
#include "storage-service/CapioFile/CapioMemoryFile.hpp"

inline SPSCQueue *cts_queue, *stc_queue;
inline CPBufResponse_t *bufs_response;
inline thread_local CircularBuffer<char> *buf_requests;

#include "../posix/utils/cache.hpp"
#include "SourceText.hpp"

WriteRequestCacheMEM *writeCache;

int test_fd                = 0;
std::string test_file_name = "test.dat";

void init_server_data_structures() {
    writeCache              = new WriteRequestCacheMEM();
    bufs_response           = new CPBufResponse_t;
    files                   = new CPFiles_t();
    capio_files_descriptors = new CPFileDescriptors_t();
    cts_queue = new SPSCQueue("queue-tests.cts", get_cache_lines(), get_cache_line_size());
    buf_requests =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);
}

void delete_server_data_structures() {
    delete cts_queue;
    delete stc_queue;
    delete writeCache;
    delete files;
    delete capio_files_descriptors;
    delete bufs_response;
    delete buf_requests;
}

TEST(CapioCacheSPSCQueue, TestCacheWithSpscQueueWrite) {

    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto tmp_buf                   = new std::unique_ptr<char>(new char[long_test_length]);

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([long_test_length, tmp_buf] {
        cts_queue->read(tmp_buf->get(), long_test_length);
        EXPECT_TRUE(strncmp(tmp_buf->get(), SOURCE_TEST_TEXT, long_test_length) == 0);
    });

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);
    writeCache->flush();

    server_thread.join();

    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestCacheSPSCQueueAndCapioFile) {
    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto readBufSize               = 1024;

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([long_test_length, readBufSize] {
        CapioMemoryFile *testFile = new CapioMemoryFile(test_file_name);
        testFile->readFromQueue(*cts_queue, 0, long_test_length);
        char *readBuf = new char[readBufSize]{};
        for (auto offset = 0; offset < long_test_length; offset += readBufSize) {
            testFile->readData(readBuf, offset, readBufSize);
            EXPECT_EQ(strncmp(readBuf, SOURCE_TEST_TEXT + offset, readBufSize), 0);
        }

        delete[] readBuf;
        delete testFile;
    });

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);
    writeCache->flush();

    server_thread.join();

    delete_server_data_structures();
}

#endif // CAPIOCACHESPSCQUEUETESTS_HPP

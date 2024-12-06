#ifndef CAPIOCACHESPSCQUEUETESTS_HPP
#define CAPIOCACHESPSCQUEUETESTS_HPP

#include "../posix/utils/env.hpp"
#include "../posix/utils/filesystem.hpp"
#include "../posix/utils/types.hpp"
#include "storage-service/CapioFile/CapioMemoryFile.hpp"

inline SPSCQueue *cts_queue, *stc_queue;
inline CPBufResponse_t *bufs_response;
inline CircularBuffer<char> *buf_requests;

#include "../posix/utils/cache.hpp"
#include "SourceText.hpp"

WriteRequestCacheMEM *writeCache;
ReadRequestCacheMEM *readCache;

int test_fd                = 0;
std::string test_file_name = "test.dat";

void init_server_data_structures() {
    writeCache              = new WriteRequestCacheMEM();
    readCache               = new ReadRequestCacheMEM();
    bufs_response           = new CPBufResponse_t;
    files                   = new CPFiles_t();
    capio_files_descriptors = new CPFileDescriptors_t();
    cts_queue = new SPSCQueue("queue-tests.cts", get_cache_lines(), get_cache_line_size());
    stc_queue = new SPSCQueue("queue-tests.stc", get_cache_lines(), get_cache_line_size());
    buf_requests =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);
}

void delete_server_data_structures() {
    delete cts_queue;
    delete stc_queue;
    delete writeCache;
    delete readCache;
    delete files;
    delete capio_files_descriptors;
    delete bufs_response;
    delete buf_requests;
}

class WriteMemReqWrapper : public WriteRequestCacheMEM {
  public:
    void request(const off64_t count, const long tid, const char *path,
                 const capio_off64_t offset) const {
        this->write_request(count, tid, path, offset);
    }
};

TEST(CapioCacheSPSCQueue, TestWriteCacheWithSpscQueueWrite) {

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

TEST(CapioCacheSPSCQueue, TestWriteCacheSPSCQueueAndCapioFile) {
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

TEST(CapioCacheSPSCQueue, TestWriteCacheSPSCQueueAndCapioFileWithRequest) {
    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto readBufSize               = 1024;

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([readBufSize] {
        char *req = new char[CAPIO_REQ_MAX_SIZE];
        int code, fd;
        pid_t tid;
        char path[PATH_MAX];
        off64_t write_size;
        capio_off64_t offset;

        buf_requests->read(req, CAPIO_REQ_MAX_SIZE);

        auto [ptr, ec] = std::from_chars(req, req + 4, code);
        EXPECT_EQ(ec, std::errc());
        strcpy(req, ptr + 1);
        EXPECT_EQ(code, CAPIO_REQUEST_WRITE_MEM);
        sscanf(req, "%ld %s %llu %ld", &tid, path, &offset, &write_size);
        EXPECT_EQ(strcmp(path, test_file_name.c_str()), 0);

        CapioMemoryFile *testFile = new CapioMemoryFile(path);
        testFile->readFromQueue(*cts_queue, 0, write_size);

        char *readBuf = new char[readBufSize]{};
        for (auto offset = 0; offset < write_size; offset += readBufSize) {
            testFile->readData(readBuf, offset, readBufSize);
            EXPECT_EQ(strncmp(readBuf, SOURCE_TEST_TEXT + offset, readBufSize), 0);
        }

        delete[] readBuf;
        delete[] req;
        delete testFile;
    });

    WriteMemReqWrapper wrapper;

    wrapper.request(long_test_length, gettid(), test_file_name.c_str(), 0);

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);
    writeCache->flush();

    server_thread.join();

    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestWriteCacheSPSCQueueAndCapioFileWithRequestAndSeek) {
    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto readBufSize               = 1024;

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([readBufSize, long_test_length] {
        char *req = new char[CAPIO_REQ_MAX_SIZE];
        int code;
        pid_t tid;
        char path[PATH_MAX];
        off64_t write_size;
        capio_off64_t offset, total_read_size = 0;
        CapioMemoryFile *testFile = nullptr;

        while (total_read_size < 2 * long_test_length) {
            buf_requests->read(req, CAPIO_REQ_MAX_SIZE);

            auto [ptr, ec] = std::from_chars(req, req + 4, code);
            EXPECT_EQ(ec, std::errc());
            strcpy(req, ptr + 1);
            EXPECT_EQ(code, CAPIO_REQUEST_WRITE_MEM);
            sscanf(req, "%ld %s %llu %ld", &tid, path, &offset, &write_size);
            EXPECT_EQ(strcmp(path, test_file_name.c_str()), 0);

            if (testFile == nullptr) {
                testFile = new CapioMemoryFile(path);
            }

            testFile->readFromQueue(*cts_queue, 0, write_size);

            char *readBuf = new char[readBufSize]{};
            for (auto offset = 0; offset < write_size; offset += readBufSize) {
                testFile->readData(readBuf, offset, readBufSize);
                EXPECT_EQ(strncmp(readBuf, SOURCE_TEST_TEXT + offset, readBufSize), 0);
            }

            delete[] readBuf;
            total_read_size += write_size;
        }
        EXPECT_EQ(total_read_size, 2 * long_test_length);
        delete[] req;
        delete testFile;
    });

    // WriteMemReqWrapper wrapper;

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);

    // simulate a SEEK on a file
    set_capio_fd_offset(test_fd, 2 * long_test_length);

    // perform to write ad a different offset
    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);

    server_thread.join();

    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestReadCacheWithSpscQueueRead) {

    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto tmp_buf                   = new std::unique_ptr<char>(new char[long_test_length]);

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread(
        [long_test_length, tmp_buf] { stc_queue->write(SOURCE_TEST_TEXT, long_test_length); });

    readCache->read(test_fd, tmp_buf->get(), long_test_length);

    EXPECT_EQ(strcmp(SOURCE_TEST_TEXT, tmp_buf->get()), 0);

    server_thread.join();

    delete_server_data_structures();
}

#endif // CAPIOCACHESPSCQUEUETESTS_HPP

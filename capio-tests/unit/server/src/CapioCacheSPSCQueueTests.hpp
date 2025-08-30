#ifndef CAPIOCACHESPSCQUEUETESTS_HPP
#define CAPIOCACHESPSCQUEUETESTS_HPP

#include <thread>

#include "../capio-server/include/utils/configuration.hpp"

#include "../capio-posix/utils/env.hpp"
#include "../capio-posix/utils/filesystem.hpp"
#include "../capio-posix/utils/types.hpp"
#include "capio/response_queue.hpp"

#include <include/storage-service/capio_file.hpp>

inline SPSCQueue *cts_queue, *stc_queue;
inline std::unordered_map<long, ResponseQueue *> *bufs_response;
inline CircularBuffer<char> *buf_requests;

#include "../capio-posix/utils/cache.hpp"
#include "SourceText.hpp"

auto checkStringEquality = [](const std::string &a, const std::string &b) {
    size_t minLen = std::min(a.size(), b.size());
    for (size_t i = 0; i < minLen; ++i) {
        if (a[i] != b[i]) {
            std::cout << "Difference at offset " << i << ": '" << a[i] << "' vs '" << b[i] << "'\n";
        }
    }
    if (a.size() != b.size()) {
        std::cout << "Strings have different lengths. input: " << a.size()
                  << ". check: " << b.size() << std::endl;
    }
};

WriteRequestCacheMEM *writeCache;
ReadRequestCacheMEM *readCache;

int test_fd                = -1;
std::string test_file_name = "test.dat";

void init_server_data_structures() {
    writeCache              = new WriteRequestCacheMEM();
    readCache               = new ReadRequestCacheMEM();
    bufs_response           = new std::unordered_map<long, ResponseQueue *>();
    files                   = new CPFiles_t();
    capio_files_descriptors = new CPFileDescriptors_t();
    cts_queue = new SPSCQueue("queue-tests.cts", get_cache_lines(), get_cache_line_size());
    stc_queue = new SPSCQueue("queue-tests.stc", get_cache_lines(), get_cache_line_size());
    buf_requests =
        new CircularBuffer<char>(SHM_COMM_CHAN_NAME, CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE);

    auto tid = gettid();
    bufs_response->insert(
        std::make_pair(tid, new ResponseQueue(SHM_COMM_CHAN_NAME_RESP + std::to_string(tid))));
}

void delete_server_data_structures() {
    delete cts_queue;
    delete stc_queue;
    delete writeCache;
    delete readCache;
    delete files;
    delete capio_files_descriptors;
    delete buf_requests;
    for (auto itm : *bufs_response) {
        delete itm.second;
    }
    delete bufs_response;
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

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);
    writeCache->flush();

    std::thread t1([long_test_length, tmp_buf] {
        cts_queue->read(tmp_buf->get(), long_test_length);

        // In the event a flush is triggered with a write of size 0, read again
        if (strlen(tmp_buf->get()) == 0) {
            cts_queue->read(tmp_buf->get(), long_test_length);
        }

        if (strncmp(tmp_buf->get(), SOURCE_TEST_TEXT, long_test_length) != 0) {
            checkStringEquality(std::string(tmp_buf->get()), std::string(SOURCE_TEST_TEXT));
        }

        EXPECT_EQ(strncmp(tmp_buf->get(), SOURCE_TEST_TEXT, long_test_length), 0);
    });

    t1.join();

    delete tmp_buf;
    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestWriteCacheSPSCQueueAndCapioFile) {
    unsigned long long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto readBufSize               = 1024;

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    writeCache->write(test_fd, SOURCE_TEST_TEXT, long_test_length);
    writeCache->flush();

    std::thread t2([long_test_length, readBufSize] {
        CapioMemoryFile *testFile = new CapioMemoryFile(test_file_name);
        testFile->readFromQueue(*cts_queue, 0, long_test_length);
        char *readBuf = new char[readBufSize]{};
        for (unsigned long offset = 0; offset < long_test_length; offset += readBufSize) {
            testFile->readData(readBuf, offset, readBufSize);
            EXPECT_EQ(strncmp(readBuf, SOURCE_TEST_TEXT + offset, readBufSize), 0);
        }

        delete[] readBuf;
        delete testFile;
    });

    t2.join();

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
        int code;
        long int tid;
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

    std::thread t3([readBufSize, long_test_length] {
        char *req = new char[CAPIO_REQ_MAX_SIZE];
        int code;
        long int tid;
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

    // simulate Close on file
    writeCache->flush();

    t3.join();

    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestReadCacheWithSpscQueueRead) {

    capio_off64_t long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto tmp_buf                   = new std::unique_ptr<char>(new char[long_test_length]);
    auto response_tid              = gettid();

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([long_test_length, response_tid] {
        int iteration = 0;

        capio_off64_t total_data_sent = 0;
        while (total_data_sent < long_test_length) {

            char req[CAPIO_REQ_MAX_SIZE];
            int code;
            long int tid;
            capio_off64_t read_size, client_cache_line_size, read_begin_offset;
            buf_requests->read(req, CAPIO_REQ_MAX_SIZE);

            auto [ptr, ec] = std::from_chars(req, req + 4, code);
            EXPECT_EQ(ec, std::errc());
            strcpy(req, ptr + 1);
            EXPECT_EQ(code, CAPIO_REQUEST_READ_MEM);
            sscanf(req, "%ld %llu %llu %llu", &tid, &read_begin_offset, &read_size,
                   &client_cache_line_size);

            auto size_to_send =
                read_size < client_cache_line_size ? read_size : client_cache_line_size;

            bufs_response->at(response_tid)->write(size_to_send);
            stc_queue->write(SOURCE_TEST_TEXT + read_begin_offset, size_to_send);
            total_data_sent += size_to_send;
            iteration++;
        }
    });

    readCache->read(test_fd, tmp_buf->get(), long_test_length);
    auto result = strncmp(SOURCE_TEST_TEXT, tmp_buf->get(), long_test_length);
    EXPECT_EQ(result, 0);
    server_thread.join();
    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestReadCacheWithSpscQueueReadWithCapioFile) {

    capio_off64_t long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto tmp_buf                   = new std::unique_ptr<char>(new char[long_test_length]);
    auto response_tid              = gettid();

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([long_test_length, response_tid] {
        int iteration = 0;
        CapioMemoryFile testFile(test_file_name);

        testFile.writeData(SOURCE_TEST_TEXT, 0, long_test_length);

        capio_off64_t total_data_sent = 0;
        while (total_data_sent < long_test_length) {

            char req[CAPIO_REQ_MAX_SIZE];
            int code;
            long int tid;
            capio_off64_t read_size, client_cache_line_size, read_begin_offset;
            buf_requests->read(req, CAPIO_REQ_MAX_SIZE);

            auto [ptr, ec] = std::from_chars(req, req + 4, code);
            EXPECT_EQ(ec, std::errc());
            strcpy(req, ptr + 1);
            EXPECT_EQ(code, CAPIO_REQUEST_READ_MEM);
            sscanf(req, "%ld %llu %llu %llu", &tid, &read_begin_offset, &read_size,
                   &client_cache_line_size);

            auto size_to_send =
                read_size < client_cache_line_size ? read_size : client_cache_line_size;

            bufs_response->at(response_tid)->write(size_to_send);
            testFile.writeToQueue(*stc_queue, read_begin_offset, size_to_send);
            total_data_sent += size_to_send;
            iteration++;
        }
    });

    readCache->read(test_fd, tmp_buf->get(), long_test_length);
    auto result = strncmp(SOURCE_TEST_TEXT, tmp_buf->get(), long_test_length);
    EXPECT_EQ(result, 0);
    server_thread.join();
    delete_server_data_structures();
}

TEST(CapioCacheSPSCQueue, TestReadCacheWithSpscQueueReadWithCapioFileAndSeek) {

    capio_off64_t long_test_length = strlen(SOURCE_TEST_TEXT) + 1;
    auto tmp_buf                   = new std::unique_ptr<char>(new char[long_test_length]);
    auto response_tid              = gettid();

    init_server_data_structures();

    capio_files_descriptors->emplace(test_fd, test_file_name);
    files->insert({test_fd, {std::make_shared<capio_off64_t>(0), 0, 0, false}});

    std::thread server_thread([long_test_length, response_tid] {
        START_LOG(gettid(), "call(server_instance)");
        int iteration = 0;
        CapioMemoryFile testFile(test_file_name);

        testFile.writeData(SOURCE_TEST_TEXT, 0, long_test_length);
        LOG("Wrote data to server test file");

        capio_off64_t total_data_sent = 0;
        while (total_data_sent < 2000) {

            char req[CAPIO_REQ_MAX_SIZE]{0};
            char file[1024];
            int code, use_cache;
            long int tid;
            capio_off64_t read_size, client_cache_line_size, read_begin_offset;

            buf_requests->read(req, CAPIO_REQ_MAX_SIZE);
            LOG("Received request: %s", req);
            sscanf(req, "%d %ld %llu %llu %llu %d %s", &code, &tid, &read_begin_offset, &read_size,
                   &client_cache_line_size, &use_cache, file);
            EXPECT_EQ(code, CAPIO_REQUEST_READ_MEM);
            LOG("code: %d", code);
            LOG("tid: %ld", tid);
            LOG("read_begin_offset: %lld", read_begin_offset);
            LOG("read_size: %lld", read_size);
            LOG("client_cache_line_size: %lld", client_cache_line_size);
            LOG("use_cache: %lld", use_cache);
            LOG("file: %s", file);
            auto size_to_send =
                read_size < client_cache_line_size ? read_size : client_cache_line_size;
            LOG("Sending %ld data", size_to_send);
            bufs_response->at(response_tid)->write(size_to_send);
            testFile.writeToQueue(*stc_queue, read_begin_offset, size_to_send);
            total_data_sent += size_to_send;
            iteration++;
        }
    });

    auto tmp_buf_ptr = tmp_buf->get();

    readCache->read(test_fd, tmp_buf_ptr, 1000);
    EXPECT_EQ(strncmp(SOURCE_TEST_TEXT, tmp_buf->get(), 1000), 0);

    // emulate seek
    auto new_offset = get_capio_fd_offset(test_fd) + 1000;
    set_capio_fd_offset(test_fd, new_offset);

    tmp_buf_ptr += new_offset;
    readCache->read(test_fd, tmp_buf_ptr, 1000);
    // checkStringEquality(SOURCE_TEST_TEXT + new_offset, tmp_buf->get() + new_offset);
    EXPECT_EQ(strncmp(SOURCE_TEST_TEXT + new_offset, tmp_buf->get() + new_offset, 1000), 0);

    server_thread.join();
    delete_server_data_structures();
}

#endif // CAPIOCACHESPSCQUEUETESTS_HPP
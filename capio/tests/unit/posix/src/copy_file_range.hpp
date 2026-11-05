#ifndef CAPIO_TEST_UNIT_POSIX_COPY_FILE_RANGE_HPP
#define CAPIO_TEST_UNIT_POSIX_COPY_FILE_RANGE_HPP

#include <gtest/gtest.h>

#include <climits>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <semaphore.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "handlers/copy_file_range.hpp"

#include "utils/common.hpp"
#include "utils/data.hpp"
#include "utils/filesystem.hpp"
#include "utils/requests.hpp"
#include "utils/types.hpp"

/*
 * The helpers copy_file_range is built on. Descriptors of the kernel are real files; for CAPIO
 * descriptors there is no server: a test writes in advance the replies the server would send, and
 * reads back the requests the client sent.
 */
class CopyFileRangeHandlerTest : public testing::Test {
  protected:
    // Descriptors CAPIO handles: only entries of the CAPIO descriptor table, never open.
    static constexpr int CAPIO_FD_IN  = 1001;
    static constexpr int CAPIO_FD_OUT = 1002;

    long tid = 0;
    std::filesystem::path directory;
    std::vector<int> kernel_fds;
    sem_t *requests_available = nullptr;

    static void SetUpTestSuite() { init_filesystem(); }
    static void TearDownTestSuite() { destroy_filesystem(); }

    void SetUp() override {
        // Each test has its own client, so the shared-memory buffers of a test never hold data
        // left by another one.
        static long next_tid = 3100;
        tid                  = next_tid++;
        init_client(tid);
        initialize_data_queues(tid);
        const std::string channel = get_capio_workflow_name() + SHM_SEM_ELEMS + SHM_COMM_CHAN_NAME;
        requests_available        = sem_open(channel.c_str(), 0);
        ASSERT_NE(requests_available, SEM_FAILED);

        directory =
            std::filesystem::temp_directory_path() /
            ("capio_copy_file_range_utils_" + std::to_string(getpid()) + "_" + std::to_string(tid));
        std::filesystem::create_directories(directory);
    }

    void TearDown() override {
        for (const int fd : kernel_fds) {
            close(fd);
        }
        for (const int fd : {CAPIO_FD_IN, CAPIO_FD_OUT}) {
            if (exists_capio_fd(fd)) {
                delete_capio_fd(fd);
            }
        }
        sem_close(requests_available);
        // The destructors remove the shared memory of the client.
        delete read_cache;
        delete write_cache;
        delete buf_requests;
        delete buff_response;
        std::filesystem::remove_all(directory);
    }

    // A file of the kernel holding `content`, open with `flags`, at offset 0.
    int kernel_file(const std::string &name, const std::string &content, const int flags = O_RDWR) {
        const auto path = directory / name;
        const int fd    = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
        EXPECT_GE(fd, 0);
        EXPECT_EQ(write(fd, content.data(), content.size()), static_cast<ssize_t>(content.size()));
        close(fd);
        const int reopened = open(path.c_str(), flags);
        EXPECT_GE(reopened, 0);
        kernel_fds.push_back(reopened);
        return reopened;
    }

    std::string file_content(const std::string &name) const {
        std::string content;
        const int fd = open((directory / name).c_str(), O_RDONLY);
        char buffer[4096];
        ssize_t n;
        while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, n);
        }
        close(fd);
        return content;
    }

    // A descriptor CAPIO handles, at file offset `offset`.
    void capio_fd(const int fd, const char *path, const off64_t offset, const int flags = O_RDWR) {
        add_capio_fd(tid, path, fd, offset, 0, flags, false);
    }

    // The replies the server sends to the next requests, in order.
    static void server_replies(const std::vector<off64_t> &values) {
        for (const off64_t value : values) {
            buff_response->write(&value);
        }
    }

    int pending_requests() const {
        int value = 0;
        sem_getvalue(requests_available, &value);
        return value;
    }

    // The next request the client sent to the server.
    static std::string next_request() {
        char request[CAPIO_REQ_MAX_SIZE];
        buf_requests->read(request);
        return request;
    }

    static std::string expected_request(const char *format, ...) {
        char request[CAPIO_REQ_MAX_SIZE];
        va_list args;
        va_start(args, format);
        vsnprintf(request, sizeof(request), format, args);
        va_end(args);
        return request;
    }
};

TEST_F(CopyFileRangeHandlerTest, maxRwCountIsIntMaxRoundedDownToAPage) {
    const auto page = static_cast<off64_t>(sysconf(_SC_PAGESIZE));
    EXPECT_EQ(capio_max_rw_count() % page, 0);
    EXPECT_LE(capio_max_rw_count(), INT_MAX);
    EXPECT_GT(capio_max_rw_count(), INT_MAX - page);
}

TEST_F(CopyFileRangeHandlerTest, accessModesOfAnEnd) {
    CPCopyFileRangeEnd_t end;
    end.flags = O_RDONLY;
    EXPECT_TRUE(end.isReadable());
    EXPECT_FALSE(end.isWritable());
    end.flags = O_WRONLY | O_APPEND;
    EXPECT_FALSE(end.isReadable());
    EXPECT_TRUE(end.isWritable());
    end.flags = O_RDWR;
    EXPECT_TRUE(end.isReadable());
    EXPECT_TRUE(end.isWritable());
    // the access mode of descriptors used only with ioctl
    end.flags = O_ACCMODE;
    EXPECT_FALSE(end.isReadable());
    EXPECT_FALSE(end.isWritable());
    end.flags = O_PATH | O_RDWR;
    EXPECT_FALSE(end.isReadable());
    EXPECT_FALSE(end.isWritable());
}

TEST_F(CopyFileRangeHandlerTest, endOfAKernelFileStartsAtItsFileOffset) {
    const int fd = kernel_file("in", "abcdefgh");
    ASSERT_EQ(lseek(fd, 3, SEEK_SET), 3);

    const auto end = capio_copy_file_range_end(fd, nullptr, tid);
    EXPECT_TRUE(end.is_open);
    EXPECT_FALSE(end.is_capio);
    EXPECT_TRUE(end.is_regular);
    EXPECT_FALSE(end.is_dir);
    EXPECT_EQ(end.flags & O_ACCMODE, O_RDWR);
    EXPECT_EQ(end.size, 8);
    EXPECT_EQ(end.position, 3);
    // nothing is asked to the server for a descriptor of the kernel
    EXPECT_EQ(pending_requests(), 0);
}

TEST_F(CopyFileRangeHandlerTest, endOfAKernelFileWithAnOffsetStartsAtTheOffset) {
    const int fd  = kernel_file("in", "abcdefgh");
    loff_t offset = 5;

    const auto end = capio_copy_file_range_end(fd, &offset, tid);
    EXPECT_EQ(end.position, 5);
    EXPECT_EQ(end.offset, &offset);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);
}

TEST_F(CopyFileRangeHandlerTest, endOfAKernelDirectoryAndOfAPipe) {
    const int dir = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    ASSERT_GE(dir, 0);
    kernel_fds.push_back(dir);
    const auto dir_end = capio_copy_file_range_end(dir, nullptr, tid);
    EXPECT_TRUE(dir_end.is_open);
    EXPECT_TRUE(dir_end.is_dir);
    EXPECT_FALSE(dir_end.is_regular);

    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);
    kernel_fds.insert(kernel_fds.end(), {pipe_fds[0], pipe_fds[1]});
    const auto pipe_end = capio_copy_file_range_end(pipe_fds[0], nullptr, tid);
    EXPECT_TRUE(pipe_end.is_open);
    EXPECT_FALSE(pipe_end.is_dir);
    EXPECT_FALSE(pipe_end.is_regular);
}

TEST_F(CopyFileRangeHandlerTest, endOfADescriptorThatIsNotOpen) {
    const int fd = kernel_file("in", "abcd");
    ASSERT_EQ(close(fd), 0);
    kernel_fds.clear();

    EXPECT_FALSE(capio_copy_file_range_end(fd, nullptr, tid).is_open);
    EXPECT_FALSE(capio_copy_file_range_end(-1, nullptr, tid).is_open);
}

TEST_F(CopyFileRangeHandlerTest, endOfACapioFileAsksTheServerForItsSize) {
    capio_fd(CAPIO_FD_IN, "/capio/in", 3, O_RDONLY);
    server_replies({10, 0}); // size, is_dir

    const auto end = capio_copy_file_range_end(CAPIO_FD_IN, nullptr, tid);
    EXPECT_TRUE(end.is_open);
    EXPECT_TRUE(end.is_capio);
    EXPECT_TRUE(end.is_regular);
    EXPECT_FALSE(end.is_dir);
    EXPECT_EQ(end.flags & O_ACCMODE, O_RDONLY);
    EXPECT_EQ(end.size, 10);
    EXPECT_EQ(end.saved_fd_offset, 3);
    EXPECT_EQ(end.position, 3);

    ASSERT_EQ(pending_requests(), 1);
    EXPECT_EQ(next_request(), expected_request("%04d %ld %d", CAPIO_REQUEST_FSTAT, tid,
                                               static_cast<int>(CAPIO_FD_IN)));
}

TEST_F(CopyFileRangeHandlerTest, endOfACapioFileWithAnOffsetKeepsTheFileOffset) {
    capio_fd(CAPIO_FD_IN, "/capio/in", 3);
    server_replies({10, 0});
    loff_t offset = 7;

    const auto end = capio_copy_file_range_end(CAPIO_FD_IN, &offset, tid);
    EXPECT_EQ(end.position, 7);
    EXPECT_EQ(end.saved_fd_offset, 3);
}

TEST_F(CopyFileRangeHandlerTest, endOfACapioDirectory) {
    capio_fd(CAPIO_FD_IN, "/capio/dir", 0, O_RDONLY | O_DIRECTORY);
    server_replies({4096, 1});

    const auto end = capio_copy_file_range_end(CAPIO_FD_IN, nullptr, tid);
    EXPECT_TRUE(end.is_dir);
    EXPECT_FALSE(end.is_regular);
}

TEST_F(CopyFileRangeHandlerTest, kernelReadWithAnOffsetLeavesTheFileOffset) {
    const int fd   = kernel_file("in", "abcdefgh");
    loff_t offset  = 2;
    auto end       = capio_copy_file_range_end(fd, &offset, tid);
    char buffer[4] = {};

    EXPECT_EQ(capio_copy_file_range_read(end, buffer, 4, tid), 4);
    EXPECT_EQ(std::string(buffer, 4), "cdef");
    EXPECT_EQ(end.position, 6);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);
}

TEST_F(CopyFileRangeHandlerTest, kernelReadWithoutAnOffsetMovesTheFileOffset) {
    const int fd   = kernel_file("in", "abcdefgh");
    auto end       = capio_copy_file_range_end(fd, nullptr, tid);
    char buffer[4] = {};

    EXPECT_EQ(capio_copy_file_range_read(end, buffer, 4, tid), 4);
    EXPECT_EQ(std::string(buffer, 4), "abcd");
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 4);
    // and at the end of the file, a read returns 0
    EXPECT_EQ(capio_copy_file_range_read(end, buffer, 4, tid), 4);
    EXPECT_EQ(capio_copy_file_range_read(end, buffer, 4, tid), 0);
}

TEST_F(CopyFileRangeHandlerTest, kernelReadOfAWriteOnlyFileFails) {
    const int fd   = kernel_file("in", "abcd", O_WRONLY);
    auto end       = capio_copy_file_range_end(fd, nullptr, tid);
    char buffer[4] = {};

    EXPECT_EQ(capio_copy_file_range_read(end, buffer, 4, tid), -EBADF);
}

TEST_F(CopyFileRangeHandlerTest, kernelWriteWithAndWithoutAnOffset) {
    const int fd  = kernel_file("out", "12345678");
    loff_t offset = 4;
    auto with     = capio_copy_file_range_end(fd, &offset, tid);
    EXPECT_EQ(capio_copy_file_range_write(with, "xy", 2, tid), 2);
    EXPECT_EQ(with.position, 6);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);

    auto without = capio_copy_file_range_end(fd, nullptr, tid);
    EXPECT_EQ(capio_copy_file_range_write(without, "ab", 2, tid), 2);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 2);
    EXPECT_EQ(file_content("out"), "ab34xy78");
}

TEST_F(CopyFileRangeHandlerTest, writeAllReportsAWriteThatFails) {
    const int fd  = kernel_file("out", "", O_RDONLY);
    auto end      = capio_copy_file_range_end(fd, nullptr, tid);
    off64_t error = 0;

    EXPECT_EQ(capio_copy_file_range_write_all(end, "abcd", 4, error, tid), 0);
    EXPECT_EQ(error, -EBADF);
}

TEST_F(CopyFileRangeHandlerTest, unreadGivesBackTheBytesNotCopied) {
    const int fd  = kernel_file("in", "abcdefgh");
    loff_t offset = 1;
    auto with     = capio_copy_file_range_end(fd, &offset, tid);
    with.position = 7;
    capio_copy_file_range_unread(with, 3);
    EXPECT_EQ(with.position, 4);

    ASSERT_EQ(lseek(fd, 6, SEEK_SET), 6);
    auto without = capio_copy_file_range_end(fd, nullptr, tid);
    capio_copy_file_range_unread(without, 2);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 4);
}

TEST_F(CopyFileRangeHandlerTest, finishOfAKernelEndAdvancesTheOffsetOfTheCaller) {
    const int fd  = kernel_file("in", "abcdefgh");
    loff_t offset = 2;
    auto with     = capio_copy_file_range_end(fd, &offset, tid);
    capio_copy_file_range_finish(with, 5, tid);
    EXPECT_EQ(offset, 7);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);

    // without an offset, the reads and writes already moved the file offset
    ASSERT_EQ(lseek(fd, 3, SEEK_SET), 3);
    auto without = capio_copy_file_range_end(fd, nullptr, tid);
    capio_copy_file_range_finish(without, 5, tid);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 3);
    EXPECT_EQ(pending_requests(), 0);
}

TEST_F(CopyFileRangeHandlerTest, throughBufferCopiesMoreThanOneChunk) {
    // more than three cache lines, and not a multiple of one
    std::string content(3 * get_cache_line_size() + 17, '\0');
    for (size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<char>('a' + i % 26);
    }
    const int in  = kernel_file("in", content, O_RDONLY);
    const int out = kernel_file("out", "", O_WRONLY);
    auto in_end   = capio_copy_file_range_end(in, nullptr, tid);
    auto out_end  = capio_copy_file_range_end(out, nullptr, tid);
    off64_t error = 0;

    EXPECT_EQ(capio_copy_file_range_through_buffer(in_end, out_end, content.size(), error, tid),
              static_cast<off64_t>(content.size()));
    EXPECT_EQ(error, 0);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), static_cast<off_t>(content.size()));
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), static_cast<off_t>(content.size()));
    EXPECT_EQ(file_content("out"), content);
}

TEST_F(CopyFileRangeHandlerTest, throughBufferWithOffsetsLeavesTheFileOffsets) {
    const int in   = kernel_file("in", "abcdefgh", O_RDONLY);
    const int out  = kernel_file("out", "12345678", O_WRONLY);
    loff_t off_in  = 2;
    loff_t off_out = 1;
    auto in_end    = capio_copy_file_range_end(in, &off_in, tid);
    auto out_end   = capio_copy_file_range_end(out, &off_out, tid);
    off64_t error  = 0;

    EXPECT_EQ(capio_copy_file_range_through_buffer(in_end, out_end, 4, error, tid), 4);
    EXPECT_EQ(in_end.position, 6);
    EXPECT_EQ(out_end.position, 5);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 0);
    EXPECT_EQ(lseek(out, 0, SEEK_CUR), 0);
    EXPECT_EQ(file_content("out"), "1cdef678");
}

TEST_F(CopyFileRangeHandlerTest, throughBufferStopsAtTheEndOfTheInput) {
    const int in  = kernel_file("in", "abcd", O_RDONLY);
    const int out = kernel_file("out", "", O_WRONLY);
    auto in_end   = capio_copy_file_range_end(in, nullptr, tid);
    auto out_end  = capio_copy_file_range_end(out, nullptr, tid);
    off64_t error = 0;

    EXPECT_EQ(capio_copy_file_range_through_buffer(in_end, out_end, 100, error, tid), 4);
    EXPECT_EQ(error, 0);
    EXPECT_EQ(capio_copy_file_range_through_buffer(in_end, out_end, 100, error, tid), 0);
    EXPECT_EQ(error, 0);
}

TEST_F(CopyFileRangeHandlerTest, throughBufferReportsAReadThatFails) {
    const int in  = kernel_file("in", "abcd", O_WRONLY);
    const int out = kernel_file("out", "", O_WRONLY);
    auto in_end   = capio_copy_file_range_end(in, nullptr, tid);
    auto out_end  = capio_copy_file_range_end(out, nullptr, tid);
    off64_t error = 0;

    EXPECT_EQ(capio_copy_file_range_through_buffer(in_end, out_end, 4, error, tid), 0);
    EXPECT_EQ(error, -EBADF);
}

TEST_F(CopyFileRangeHandlerTest, throughBufferGivesBackTheInputAfterAWriteThatFails) {
    // The output cannot grow past 4096 bytes: of the 8192 bytes read, only 4096 are written, the
    // next write fails with EFBIG, and the input goes back to the first byte not copied.
    const std::string content(8192, 'x');
    const int in         = kernel_file("in", content, O_RDONLY);
    const int out        = kernel_file("out", "", O_WRONLY);
    loff_t off_in        = 0;
    auto implicit        = capio_copy_file_range_end(in, nullptr, tid);
    auto explicit_offset = capio_copy_file_range_end(in, &off_in, tid);
    auto out_end         = capio_copy_file_range_end(out, nullptr, tid);

    rlimit previous{};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &previous), 0);
    const rlimit limited{4096, previous.rlim_max};
    // without this, a write past the limit kills the process with SIGXFSZ
    const auto previous_handler = signal(SIGXFSZ, SIG_IGN);
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &limited), 0);

    off64_t error = 0;
    const off64_t copied =
        capio_copy_file_range_through_buffer(implicit, out_end, 8192, error, tid);
    // the same, with the input read at an offset passed by the caller
    ASSERT_EQ(ftruncate(out, 0), 0);
    ASSERT_EQ(lseek(out, 0, SEEK_SET), 0);
    off64_t explicit_error = 0;
    const off64_t explicit_copied =
        capio_copy_file_range_through_buffer(explicit_offset, out_end, 8192, explicit_error, tid);

    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &previous), 0);
    signal(SIGXFSZ, previous_handler);

    EXPECT_EQ(copied, 4096);
    EXPECT_EQ(error, -EFBIG);
    EXPECT_EQ(lseek(in, 0, SEEK_CUR), 4096);
    EXPECT_EQ(explicit_copied, 4096);
    EXPECT_EQ(explicit_error, -EFBIG);
    EXPECT_EQ(explicit_offset.position, 4096);
}

TEST_F(CopyFileRangeHandlerTest, seekOfACapioEndOnlyWhenTheFileOffsetDiffers) {
    capio_fd(CAPIO_FD_IN, "/capio/in", 3);
    CPCopyFileRangeEnd_t end;
    end.fd       = CAPIO_FD_IN;
    end.is_capio = true;

    end.position = 3;
    capio_copy_file_range_seek(end, tid);
    EXPECT_EQ(pending_requests(), 0);

    end.position = 9;
    server_replies({9});
    capio_copy_file_range_seek(end, tid);
    EXPECT_EQ(get_capio_fd_offset(CAPIO_FD_IN), 9);
    ASSERT_EQ(pending_requests(), 1);
    EXPECT_EQ(next_request(), expected_request("%04d %ld %d %zu", CAPIO_REQUEST_SEEK, tid,
                                               static_cast<int>(CAPIO_FD_IN), 9UL));
}

TEST_F(CopyFileRangeHandlerTest, onServerAsksUntilTheEndOfTheInput) {
    CPCopyFileRangeEnd_t in, out;
    in.fd        = CAPIO_FD_IN;
    in.position  = 2;
    out.fd       = CAPIO_FD_OUT;
    out.position = 5;
    // the server copies 4 bytes, then 3, then reaches the end of the input
    server_replies({4, 3, 0});

    EXPECT_EQ(capio_copy_file_range_on_server(in, out, 100, tid), 7);
    EXPECT_EQ(in.position, 9);
    EXPECT_EQ(out.position, 12);

    // each request starts where the previous one stopped
    ASSERT_EQ(pending_requests(), 3);
    const char *format = "%04d %ld %d %d %ld %ld %ld";
    const int fd_in = CAPIO_FD_IN, fd_out = CAPIO_FD_OUT;
    EXPECT_EQ(next_request(), expected_request(format, CAPIO_REQUEST_COPY_FILE_RANGE, tid, fd_in,
                                               fd_out, 2L, 5L, 100L));
    EXPECT_EQ(next_request(), expected_request(format, CAPIO_REQUEST_COPY_FILE_RANGE, tid, fd_in,
                                               fd_out, 6L, 9L, 96L));
    EXPECT_EQ(next_request(), expected_request(format, CAPIO_REQUEST_COPY_FILE_RANGE, tid, fd_in,
                                               fd_out, 9L, 12L, 93L));
}

TEST_F(CopyFileRangeHandlerTest, onServerStopsAtTheCount) {
    CPCopyFileRangeEnd_t in, out;
    in.fd  = CAPIO_FD_IN;
    out.fd = CAPIO_FD_OUT;
    server_replies({4, 4});

    EXPECT_EQ(capio_copy_file_range_on_server(in, out, 8, tid), 8);
    EXPECT_EQ(pending_requests(), 2);
}

TEST_F(CopyFileRangeHandlerTest, finishOfACapioEnd) {
    capio_fd(CAPIO_FD_IN, "/capio/in", 3);
    capio_fd(CAPIO_FD_OUT, "/capio/out", 0);

    // with an offset passed by the caller: the offset advances, the file offset stays at 3
    loff_t offset = 10;
    CPCopyFileRangeEnd_t with;
    with.fd              = CAPIO_FD_IN;
    with.is_capio        = true;
    with.offset          = &offset;
    with.saved_fd_offset = 3;
    with.position        = 14;
    capio_copy_file_range_finish(with, 4, tid);
    EXPECT_EQ(offset, 14);
    EXPECT_EQ(get_capio_fd_offset(CAPIO_FD_IN), 3);
    EXPECT_EQ(pending_requests(), 0);

    // without: the file offset moves past the bytes copied, on the client and on the server
    CPCopyFileRangeEnd_t without;
    without.fd       = CAPIO_FD_OUT;
    without.is_capio = true;
    without.position = 6;
    server_replies({6});
    capio_copy_file_range_finish(without, 6, tid);
    EXPECT_EQ(get_capio_fd_offset(CAPIO_FD_OUT), 6);
    ASSERT_EQ(pending_requests(), 1);
    EXPECT_EQ(next_request(), expected_request("%04d %ld %d %zu", CAPIO_REQUEST_SEEK, tid,
                                               static_cast<int>(CAPIO_FD_OUT), 6UL));
}

#endif // CAPIO_TEST_UNIT_POSIX_COPY_FILE_RANGE_HPP

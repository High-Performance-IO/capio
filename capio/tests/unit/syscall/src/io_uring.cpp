#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "helpers.hpp"

namespace {

// The smallest io_uring client, without liburing: it submits one SQE and waits for its CQE.
class Ring {
    int fd = -1;
    io_uring_params params{};
    size_t sq_size = 0, cq_size = 0, sqes_size = 0;
    void *sq_ring = MAP_FAILED, *cq_ring = MAP_FAILED;
    io_uring_sqe *sqes = static_cast<io_uring_sqe *>(MAP_FAILED);

    template <typename T> T *at(void *ring, const unsigned offset) {
        return reinterpret_cast<T *>(static_cast<char *>(ring) + offset);
    }

  public:
    bool setup() {
        fd = static_cast<int>(syscall(SYS_io_uring_setup, 4, &params));
        if (fd < 0) {
            return false;
        }
        sq_size        = params.sq_off.array + params.sq_entries * sizeof(unsigned);
        cq_size        = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);
        sqes_size      = params.sq_entries * sizeof(io_uring_sqe);
        const bool one = params.features & IORING_FEAT_SINGLE_MMAP;
        if (one) {
            sq_size = cq_size = std::max(sq_size, cq_size);
        }
        sq_ring = mmap(nullptr, sq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
                       IORING_OFF_SQ_RING);
        cq_ring = one ? sq_ring
                      : mmap(nullptr, cq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                             fd, IORING_OFF_CQ_RING);
        sqes    = static_cast<io_uring_sqe *>(mmap(nullptr, sqes_size, PROT_READ | PROT_WRITE,
                                                   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES));
        return sq_ring != MAP_FAILED && cq_ring != MAP_FAILED && sqes != MAP_FAILED;
    }

    // Submits sqe and returns the result in its CQE (-errno if io_uring_enter fails).
    int run(const io_uring_sqe &sqe) {
        auto *sq_tail        = at<unsigned>(sq_ring, params.sq_off.tail);
        const unsigned mask  = *at<unsigned>(sq_ring, params.sq_off.ring_mask);
        const unsigned tail  = *sq_tail;
        const unsigned index = tail & mask;
        sqes[index]          = sqe;
        at<unsigned>(sq_ring, params.sq_off.array)[index] = index;
        __atomic_store_n(sq_tail, tail + 1, __ATOMIC_RELEASE);

        if (syscall(SYS_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS, nullptr, 0) < 0) {
            return -errno;
        }
        auto *cq_head       = at<unsigned>(cq_ring, params.cq_off.head);
        const unsigned head = *cq_head;
        if (head == __atomic_load_n(at<unsigned>(cq_ring, params.cq_off.tail), __ATOMIC_ACQUIRE)) {
            return -EAGAIN;
        }
        const unsigned cq_mask = *at<unsigned>(cq_ring, params.cq_off.ring_mask);
        const int result       = at<io_uring_cqe>(cq_ring, params.cq_off.cqes)[head & cq_mask].res;
        __atomic_store_n(cq_head, head + 1, __ATOMIC_RELEASE);
        return result;
    }

    ~Ring() {
        if (sqes != MAP_FAILED) {
            munmap(sqes, sqes_size);
        }
        if (cq_ring != MAP_FAILED && cq_ring != sq_ring) {
            munmap(cq_ring, cq_size);
        }
        if (sq_ring != MAP_FAILED) {
            munmap(sq_ring, sq_size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
};

} // namespace

/*
 * Data written to a CAPIO file through io_uring must reach CAPIO like data written with write():
 * a read of the file returns it. Not intercepted, the write goes to the file on disk, which CAPIO
 * does not read.
 */
TEST(SystemCallTest, TestIoUringWriteIsReadBack) {
    constexpr const char *PATHNAME    = "test_io_uring_file.txt";
    constexpr std::string_view BUFFER = "written through io_uring";

    if (!memory_variant() && std::getenv("LD_PRELOAD") != nullptr) {
        GTEST_SKIP() << "the FS variant does not intercept io_uring yet";
    }
    Ring ring;
    if (!ring.setup()) {
        GTEST_SKIP() << "io_uring is not available: " << std::strerror(errno);
    }
    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);

    io_uring_sqe sqe{};
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd     = fd;
    sqe.addr   = reinterpret_cast<uintptr_t>(BUFFER.data());
    sqe.len    = BUFFER.size();
    sqe.off    = 0;
    EXPECT_EQ(ring.run(sqe), static_cast<int>(BUFFER.size()));

    char buf[BUFFER.size()]{};
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(read(fd, buf, BUFFER.size()), static_cast<ssize_t>(BUFFER.size()));
    EXPECT_EQ(std::string_view(buf, BUFFER.size()), BUFFER);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
}

#ifndef CAPIO_TEST_UNIT_POSIX_URING_HPP
#define CAPIO_TEST_UNIT_POSIX_URING_HPP

#include <gtest/gtest.h>

#include <cerrno>
#include <memory>

#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#if defined(SYS_io_uring_setup)

#include "handlers/io_uring.hpp"

TEST(UringTest, EntriesAreRoundedUpToAPowerOfTwo) {
    uint32_t entries = 0;
    EXPECT_TRUE(uring_entries(5, 64, false, entries));
    EXPECT_EQ(entries, 8);
    EXPECT_TRUE(uring_entries(8, 64, false, entries));
    EXPECT_EQ(entries, 8);
    EXPECT_FALSE(uring_entries(0, 64, false, entries));
    // Above the limit: EINVAL, unless clamped to it
    EXPECT_FALSE(uring_entries(100, 64, false, entries));
    EXPECT_TRUE(uring_entries(100, 64, true, entries));
    EXPECT_EQ(entries, 64);
}

TEST(UringTest, TheCqIsTwiceTheSqUnlessItsSizeIsGiven) {
    CapioRing ring;
    io_uring_params params{};
    EXPECT_TRUE(uring_ring_sizes(4, &params, ring));
    EXPECT_EQ(ring.sq_entries, 4);
    EXPECT_EQ(ring.cq_entries, 8);

    params.flags      = IORING_SETUP_CQSIZE;
    params.cq_entries = 30;
    EXPECT_TRUE(uring_ring_sizes(4, &params, ring));
    EXPECT_EQ(ring.cq_entries, 32);
    // A CQ smaller than the SQ is EINVAL
    params.cq_entries = 2;
    EXPECT_FALSE(uring_ring_sizes(4, &params, ring));
}

TEST(UringTest, ASetupWithFlagsCapioDoesNotServeIsRefused) {
    io_uring_params params{};
    params.flags = IORING_SETUP_SQPOLL;
    errno        = 0;
    EXPECT_EQ(capio_io_uring_setup(4, &params, 1), CAPIO_POSIX_SYSCALL_ERRNO);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(capio_io_uring_setup(4, nullptr, 1), CAPIO_POSIX_SYSCALL_ERRNO);
    params.flags = 0;
    EXPECT_EQ(capio_io_uring_setup(0, &params, 1), CAPIO_POSIX_SYSCALL_ERRNO);
    // DEFER_TASKRUN needs SINGLE_ISSUER, as in the kernel
    params.flags = IORING_SETUP_DEFER_TASKRUN;
    EXPECT_EQ(capio_io_uring_setup(4, &params, 1), CAPIO_POSIX_SYSCALL_ERRNO);
}

TEST(UringTest, AnErrorOrAShortTransferBreaksAChain) {
    io_uring_sqe sqe{};
    sqe.opcode = IORING_OP_READ;
    sqe.len    = 10;
    EXPECT_TRUE(uring_breaks_link(&sqe, -EIO));
    EXPECT_TRUE(uring_breaks_link(&sqe, 9));
    EXPECT_FALSE(uring_breaks_link(&sqe, 10));

    iovec iov[2] = {{nullptr, 4}, {nullptr, 6}};
    sqe.opcode   = IORING_OP_WRITEV;
    sqe.addr     = reinterpret_cast<uintptr_t>(iov);
    sqe.len      = 2;
    EXPECT_TRUE(uring_breaks_link(&sqe, 9));
    EXPECT_FALSE(uring_breaks_link(&sqe, 10));

    sqe.opcode = IORING_OP_NOP;
    EXPECT_FALSE(uring_breaks_link(&sqe, 0));
}

TEST(UringTest, TheResultOfACqeIsMinusErrno) {
    errno = ENOENT;
    EXPECT_EQ(uring_result(CAPIO_POSIX_SYSCALL_ERRNO), -ENOENT);
    EXPECT_EQ(uring_result(12), 12);
}

TEST(UringTest, TheTableKnowsTheRingsByTheirDescriptor) {
    RingTable table;
    EXPECT_EQ(table.find(5), nullptr);
    EXPECT_TRUE(table.fdsBetween(0, 100).empty());

    auto first = std::make_shared<CapioRing>();
    table.add(5, first);
    table.add(9, std::make_shared<CapioRing>());
    EXPECT_EQ(table.find(5), first);
    EXPECT_EQ(table.find(6), nullptr);
    auto between = table.fdsBetween(4, 8);
    EXPECT_EQ(between, std::vector<int>{5});

    // A ring left under a number is replaced
    auto second = std::make_shared<CapioRing>();
    table.add(5, second);
    EXPECT_EQ(table.find(5), second);
    EXPECT_EQ(first.use_count(), 1);

    EXPECT_TRUE(table.remove(5));
    EXPECT_FALSE(table.remove(5));
    EXPECT_EQ(table.find(5), nullptr);
    EXPECT_TRUE(table.remove(9));
    EXPECT_TRUE(table.fdsBetween(0, 100).empty());
}

TEST(UringTest, CloseOfADescriptorThatIsNotARingIsNotDone) {
    long result = 7;
    EXPECT_FALSE(uring_close(123456, &result));
    EXPECT_EQ(result, 7);
}

TEST(UringTest, TheCqesThatDoNotFitWaitInOrder) {
    const auto ring = std::make_shared<CapioRing>();
    io_uring_params params{};
    ASSERT_TRUE(uring_ring_sizes(2, &params, *ring));
    params.flags      = IORING_SETUP_CQSIZE;
    params.cq_entries = 2;
    ASSERT_TRUE(uring_ring_sizes(2, &params, *ring));
    const int memfd = static_cast<int>(syscall(SYS_memfd_create, "capio_uring_test", MFD_CLOEXEC));
    ASSERT_GE(memfd, 0);
    ASSERT_TRUE(uring_layout(*ring, &params, memfd));
    if (uring_kernel_setup(*ring) < 0) {
        close(memfd);
        GTEST_SKIP() << "io_uring is not available";
    }

    for (uint64_t user_data = 1; user_data <= 3; ++user_data) {
        uring_post_cqe(*ring, user_data, static_cast<int32_t>(user_data));
    }
    EXPECT_EQ(uring_cq_ready(*ring), 2);
    EXPECT_FALSE(uring_cq_has_space(*ring));
    ASSERT_EQ(ring->overflow.size(), 1);

    // The application consumes one: the one that waited comes after the others
    EXPECT_EQ(ring->cqes[*ring->cq_head & *ring->cq_mask].user_data, 1);
    __atomic_store_n(ring->cq_head, *ring->cq_head + 1, __ATOMIC_RELEASE);
    uring_collect(*ring);
    EXPECT_TRUE(ring->overflow.empty());
    EXPECT_EQ(ring->cqes[(*ring->cq_tail - 1) & *ring->cq_mask].user_data, 3);
    close(memfd);
}

#endif // SYS_io_uring_setup
#endif // CAPIO_TEST_UNIT_POSIX_URING_HPP

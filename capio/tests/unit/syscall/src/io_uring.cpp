#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <linux/close_range.h>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "helpers.hpp"

namespace {

/// @brief The smallest io_uring client, without liburing: it queues SQEs, submits them and reaps
/// their CQEs
class Ring {
    int _fd = -1;
    io_uring_params _params{};
    size_t _sq_size = 0, _cq_size = 0, _sqes_size = 0;
    void *_sq_ring = MAP_FAILED, *_cq_ring = MAP_FAILED;
    io_uring_sqe *_sqes = static_cast<io_uring_sqe *>(MAP_FAILED);
    unsigned _queued    = 0;

    template <typename T> T *at(void *ring, const unsigned offset) {
        return reinterpret_cast<T *>(static_cast<char *>(ring) + offset);
    }

  public:
    Ring()                        = default;
    Ring(const Ring &)            = delete;
    Ring &operator=(const Ring &) = delete;

    /// @brief io_uring_setup with @p flags. @return 0, or -errno
    int setup(const unsigned flags = 0) {
        _params.flags = flags;
        _fd           = static_cast<int>(syscall(SYS_io_uring_setup, 8, &_params));
        if (_fd < 0) {
            return -errno;
        }
        _sq_size       = _params.sq_off.array + _params.sq_entries * sizeof(unsigned);
        _cq_size       = _params.cq_off.cqes + _params.cq_entries * sizeof(io_uring_cqe);
        _sqes_size     = _params.sq_entries * sizeof(io_uring_sqe);
        const bool one = _params.features & IORING_FEAT_SINGLE_MMAP;
        if (one) {
            _sq_size = _cq_size = std::max(_sq_size, _cq_size);
        }
        _sq_ring = mmap(nullptr, _sq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, _fd,
                        IORING_OFF_SQ_RING);
        _cq_ring = one ? _sq_ring
                       : mmap(nullptr, _cq_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                              _fd, IORING_OFF_CQ_RING);
        _sqes = static_cast<io_uring_sqe *>(mmap(nullptr, _sqes_size, PROT_READ | PROT_WRITE,
                                                 MAP_SHARED | MAP_POPULATE, _fd, IORING_OFF_SQES));
        return _sq_ring != MAP_FAILED && _cq_ring != MAP_FAILED && _sqes != MAP_FAILED ? 0
                                                                                       : -ENOMEM;
    }

    [[nodiscard]] int fd() const { return _fd; }

    /// @brief The descriptor is no longer the ring's: it is not closed at the end
    void forget() { _fd = -1; }

    /// @brief Queue @p sqe, submitted by the next submit
    void push(const io_uring_sqe &sqe) {
        auto *sq_tail        = at<unsigned>(_sq_ring, _params.sq_off.tail);
        const unsigned mask  = *at<unsigned>(_sq_ring, _params.sq_off.ring_mask);
        const unsigned tail  = *sq_tail;
        const unsigned index = tail & mask;
        _sqes[index]         = sqe;
        at<unsigned>(_sq_ring, _params.sq_off.array)[index] = index;
        __atomic_store_n(sq_tail, tail + 1, __ATOMIC_RELEASE);
        ++_queued;
    }

    /// @brief Submit the SQEs queued, and wait for @p wait CQEs. @return what io_uring_enter
    /// returns: the SQEs submitted, or -errno
    int submit(const unsigned wait) {
        const auto submitted = syscall(SYS_io_uring_enter, _fd, _queued, wait,
                                       wait > 0 ? IORING_ENTER_GETEVENTS : 0, nullptr, 0);
        _queued              = 0;
        return submitted < 0 ? -errno : static_cast<int>(submitted);
    }

    /// @brief The CQEs in the CQ, which are consumed
    std::vector<io_uring_cqe> reap() {
        std::vector<io_uring_cqe> cqes;
        auto *cq_head = at<unsigned>(_cq_ring, _params.cq_off.head);
        const unsigned tail =
            __atomic_load_n(at<unsigned>(_cq_ring, _params.cq_off.tail), __ATOMIC_ACQUIRE);
        const unsigned cq_mask = *at<unsigned>(_cq_ring, _params.cq_off.ring_mask);
        for (unsigned head = *cq_head; head != tail; ++head) {
            cqes.push_back(at<io_uring_cqe>(_cq_ring, _params.cq_off.cqes)[head & cq_mask]);
        }
        __atomic_store_n(cq_head, tail, __ATOMIC_RELEASE);
        return cqes;
    }

    /// @brief Submit @p sqe alone, and return the res of its CQE (-errno if io_uring_enter fails)
    int run(const io_uring_sqe &sqe) {
        push(sqe);
        if (const int submitted = submit(1); submitted < 0) {
            return submitted;
        }
        const auto cqes = reap();
        return cqes.empty() ? -EAGAIN : cqes.front().res;
    }

    ~Ring() {
        if (_sqes != MAP_FAILED) {
            munmap(_sqes, _sqes_size);
        }
        if (_cq_ring != MAP_FAILED && _cq_ring != _sq_ring) {
            munmap(_cq_ring, _cq_size);
        }
        if (_sq_ring != MAP_FAILED) {
            munmap(_sq_ring, _sq_size);
        }
        if (_fd >= 0) {
            close(_fd);
        }
    }
};

/// @brief Whether the tests run with CAPIO's FS variant, which does not implement io_uring on
/// CAPIO files yet (see TestIoUringOnACapioFileIsNotImplementedInTheFsVariant)
bool fs_variant() { return !memory_variant() && std::getenv("LD_PRELOAD") != nullptr; }

/// @brief Why the tests of io_uring cannot run here, or empty if they can: @p ring is set up
std::string unusable(Ring &ring) {
    // e.g. in a Docker container with the default seccomp profile
    if (const int error = ring.setup(); error < 0) {
        return "io_uring is not available: " + std::string(std::strerror(-error));
    }
    return {};
}

#define SKIP_WITHOUT_IO_URING(ring)                                                                \
    if (const auto reason = unusable(ring); !reason.empty()) {                                     \
        GTEST_SKIP() << reason;                                                                    \
    }

/// @brief For the tests that serve CAPIO files through a ring
#define SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)                                                 \
    if (fs_variant()) {                                                                            \
        GTEST_SKIP() << "the FS variant does not implement io_uring on CAPIO files yet";           \
    }                                                                                              \
    SKIP_WITHOUT_IO_URING(ring)

/// @brief In a child: whether @p body, which uses io_uring on a CAPIO file, ends the process as a
/// system call CAPIO does not implement does (CapioUnimplementedException)
bool is_not_implemented(const std::function<void(Ring &)> &body) {
    return run_in_child([&body] {
               Ring ring;
               if (ring.setup() < 0) {
                   return 3;
               }
               body(ring);
               return 0; // it returned: implemented
           }) == EXIT_FAILURE;
}

io_uring_sqe read_or_write(const __u8 opcode, const int fd, const void *buffer, const size_t size,
                           const __u64 offset) {
    io_uring_sqe sqe{};
    sqe.opcode = opcode;
    sqe.fd     = fd;
    sqe.addr   = reinterpret_cast<uintptr_t>(buffer);
    sqe.len    = static_cast<__u32>(size);
    sqe.off    = offset;
    return sqe;
}

io_uring_sqe on_path(const __u8 opcode, const char *path) {
    io_uring_sqe sqe{};
    sqe.opcode = opcode;
    sqe.fd     = AT_FDCWD;
    sqe.addr   = reinterpret_cast<uintptr_t>(path);
    return sqe;
}

} // namespace

/*
 * Data written to a CAPIO file through io_uring must reach CAPIO like data written with write():
 * a read of the file returns it. Not intercepted, the write goes to the file on disk, which CAPIO
 * does not read.
 */
TEST(SystemCallTest, TestIoUringWriteIsReadBack) {
    constexpr const char *PATHNAME    = "test_io_uring_file.txt";
    constexpr std::string_view BUFFER = "written through io_uring";

    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)
    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);

    EXPECT_EQ(ring.run(read_or_write(IORING_OP_WRITE, fd, BUFFER.data(), BUFFER.size(), 0)),
              static_cast<int>(BUFFER.size()));

    char buf[BUFFER.size()]{};
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(read(fd, buf, BUFFER.size()), static_cast<ssize_t>(BUFFER.size()));
    EXPECT_EQ(std::string_view(buf, BUFFER.size()), BUFFER);
    EXPECT_NE(close(fd), -1);
    EXPECT_NE(unlink(PATHNAME), -1);
}

TEST(SystemCallTest, TestIoUringReadsWhatWriteWrote) {
    constexpr const char *PATHNAME = "test_io_uring_read.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)
    write_file(PATHNAME, "written with write");
    const int fd = open(PATHNAME, O_RDONLY);
    ASSERT_NE(fd, -1);

    // At an offset, which leaves the offset of the descriptor where it was
    char buffer[16]{};
    EXPECT_EQ(ring.run(read_or_write(IORING_OP_READ, fd, buffer, 4, 8)), 4);
    EXPECT_EQ(std::string_view(buffer, 4), "with");
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 0);

    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringVectoredAtTheOffsetOfTheDescriptor) {
    constexpr const char *PATHNAME = "test_io_uring_vectored.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)
    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);

    // An offset of -1 is the offset of the descriptor, which moves
    char first[] = "first ", second[] = "second";
    iovec out[] = {{first, 6}, {second, 6}};
    EXPECT_EQ(ring.run(read_or_write(IORING_OP_WRITEV, fd, out, 2, static_cast<__u64>(-1))), 12);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 12);

    char a[6]{}, b[6]{};
    iovec in[] = {{a, 6}, {b, 6}};
    EXPECT_EQ(ring.run(read_or_write(IORING_OP_READV, fd, in, 2, 0)), 12);
    EXPECT_EQ(std::string_view(a, 6), "first ");
    EXPECT_EQ(std::string_view(b, 6), "second");

    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringOpensStatsAndClosesACapioFile) {
    constexpr const char *PATHNAME = "test_io_uring_open.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)

    io_uring_sqe open_sqe = on_path(IORING_OP_OPENAT, PATHNAME);
    open_sqe.open_flags   = O_CREAT | O_RDWR | O_TRUNC;
    open_sqe.len          = S_IRUSR | S_IWUSR; // the mode
    const int fd          = ring.run(open_sqe);
    ASSERT_GE(fd, 0) << std::strerror(-fd);
    // A CAPIO file: what write writes is in CAPIO
    EXPECT_EQ(write(fd, "1234567", 7), 7);

    struct statx info{};
    io_uring_sqe stat_sqe = on_path(IORING_OP_STATX, PATHNAME);
    stat_sqe.len          = STATX_SIZE; // the mask
    stat_sqe.addr2        = reinterpret_cast<uintptr_t>(&info);
    EXPECT_EQ(ring.run(stat_sqe), 0);
    EXPECT_EQ(info.stx_size, 7);

    io_uring_sqe close_sqe{};
    close_sqe.opcode = IORING_OP_CLOSE;
    close_sqe.fd     = fd;
    EXPECT_EQ(ring.run(close_sqe), 0);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(read_file(PATHNAME), "1234567");
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringMakesAndRemovesADirectory) {
    constexpr const char *PATHNAME = "test_io_uring_directory";
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)

    io_uring_sqe mkdir_sqe = on_path(IORING_OP_MKDIRAT, PATHNAME);
    mkdir_sqe.len          = S_IRWXU; // the mode
    EXPECT_EQ(ring.run(mkdir_sqe), 0);
    struct stat info{};
    ASSERT_EQ(stat(PATHNAME, &info), 0);
    EXPECT_TRUE(S_ISDIR(info.st_mode));

    io_uring_sqe unlink_sqe = on_path(IORING_OP_UNLINKAT, PATHNAME);
    unlink_sqe.unlink_flags = AT_REMOVEDIR;
    EXPECT_EQ(ring.run(unlink_sqe), 0);
    EXPECT_NE(access(PATHNAME, F_OK), 0); // stat of a CAPIO path waits for it to exist
}

TEST(SystemCallTest, TestIoUringLeavesOtherFilesToTheKernel) {
    Ring ring;
    SKIP_WITHOUT_IO_URING(ring)
    // Outside CAPIO_DIR: the kernel ring writes it
    char path[]  = "/tmp/capio_io_uring_XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_NE(fd, -1);

    constexpr std::string_view DATA = "to the kernel";
    EXPECT_EQ(ring.run(read_or_write(IORING_OP_WRITE, fd, DATA.data(), DATA.size(), 0)),
              static_cast<int>(DATA.size()));
    char buffer[DATA.size()]{};
    EXPECT_EQ(pread(fd, buffer, DATA.size(), 0), static_cast<ssize_t>(DATA.size()));
    EXPECT_EQ(std::string_view(buffer, DATA.size()), DATA);

    io_uring_sqe nop{};
    nop.opcode    = IORING_OP_NOP;
    nop.user_data = 42;
    ring.push(nop);
    EXPECT_EQ(ring.submit(1), 1);
    const auto cqes = ring.reap();
    ASSERT_EQ(cqes.size(), 1);
    EXPECT_EQ(cqes[0].user_data, 42);
    EXPECT_EQ(cqes[0].res, 0);

    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(path), 0);
}

TEST(SystemCallTest, TestIoUringAFailureCancelsTheRestOfItsChain) {
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)

    // The unlink of a CAPIO file that does not exist fails, and the SQE linked to it is cancelled
    io_uring_sqe missing = on_path(IORING_OP_UNLINKAT, "test_io_uring_missing.txt");
    missing.flags        = IOSQE_IO_LINK;
    missing.user_data    = 1;
    io_uring_sqe linked{};
    linked.opcode    = IORING_OP_NOP;
    linked.user_data = 2;
    ring.push(missing);
    ring.push(linked);
    EXPECT_EQ(ring.submit(2), 2);
    const auto cqes = ring.reap();
    ASSERT_EQ(cqes.size(), 2);
    EXPECT_EQ(cqes[0].user_data, 1);
    EXPECT_EQ(cqes[0].res, -ENOENT);
    EXPECT_EQ(cqes[1].user_data, 2);
    EXPECT_EQ(cqes[1].res, -ECANCELED);
}

/*
 * What CAPIO does not implement on a CAPIO file ends the program, as the system calls it does not
 * implement do: an opcode it does not serve, a chain of CAPIO files and other files, and the
 * registration of a CAPIO file.
 */
TEST(SystemCallTest, TestIoUringWhatIsNotImplementedOnACapioFileEndsTheProgram) {
    constexpr const char *PATHNAME = "test_io_uring_unimplemented.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING_ON_CAPIO_FILES(ring)
    write_file(PATHNAME, "data");

    EXPECT_TRUE(is_not_implemented([PATHNAME](Ring &child) {
        io_uring_sqe fadvise{};
        fadvise.opcode = IORING_OP_FADVISE;
        fadvise.fd     = open(PATHNAME, O_RDONLY);
        child.run(fadvise);
    }));
    EXPECT_TRUE(is_not_implemented([PATHNAME](Ring &child) {
        const int capio = open(PATHNAME, O_RDONLY);
        const int other = open("/dev/null", O_RDONLY);
        char buffer[4];
        io_uring_sqe first = read_or_write(IORING_OP_READ, capio, buffer, 4, 0);
        first.flags        = IOSQE_IO_LINK;
        child.push(first);
        child.push(read_or_write(IORING_OP_READ, other, buffer, 4, 0));
        child.submit(2);
    }));
    EXPECT_TRUE(is_not_implemented([PATHNAME](Ring &child) {
        const int capio = open(PATHNAME, O_RDONLY);
        syscall(SYS_io_uring_register, child.fd(), IORING_REGISTER_FILES, &capio, 1);
    }));
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringOnACapioFileIsNotImplementedInTheFsVariant) {
    if (!fs_variant()) {
        GTEST_SKIP() << "the memory variant implements it";
    }
    constexpr const char *PATHNAME = "test_io_uring_fs.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING(ring)
    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    EXPECT_TRUE(is_not_implemented(
        [fd](Ring &child) { child.run(read_or_write(IORING_OP_WRITE, fd, "data", 4, 0)); }));
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringRegistersOtherFilesButNotTheRing) {
    Ring ring;
    SKIP_WITHOUT_IO_URING(ring)
    // Another file goes to the kernel ring (a CAPIO file: see the test above)
    const int other = open("/dev/null", O_RDONLY);
    ASSERT_NE(other, -1);
    EXPECT_EQ(syscall(SYS_io_uring_register, ring.fd(), IORING_REGISTER_FILES, &other, 1), 0);

    // io_uring_enter must get the descriptor of the ring CAPIO serves
    io_uring_rsrc_update update{};
    update.offset = static_cast<__u32>(-1);
    update.data   = static_cast<__u64>(ring.fd());
    EXPECT_EQ(syscall(SYS_io_uring_register, ring.fd(), IORING_REGISTER_RING_FDS, &update, 1), -1);
    EXPECT_EQ(errno, EOPNOTSUPP);
    EXPECT_EQ(close(other), 0);
}

TEST(SystemCallTest, TestIoUringRefusesASetupItCannotServe) {
    Ring served;
    SKIP_WITHOUT_IO_URING(served)
    Ring polled; // a kernel thread polls the SQ: CAPIO cannot
    EXPECT_EQ(polled.setup(IORING_SETUP_SQPOLL), -EINVAL);
}

namespace {

/// @brief Whether @p fd is a CAPIO file: its offset is CAPIO's, and closed it is no descriptor
bool closes_as_a_capio_file(const int fd) {
    if (close(fd) != 0) {
        return false;
    }
    return lseek(fd, 0, SEEK_CUR) == -1 && errno == EBADF;
}

} // namespace

/*
 * A ring closed by close_range, or replaced by dup2, leaves the table of rings: else the next file
 * with its number would be closed as the ring, and stay a CAPIO file once closed.
 */
TEST(SystemCallTest, TestIoUringARingClosedByCloseRangeFreesItsNumber) {
    constexpr const char *PATHNAME = "test_io_uring_close_range.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING(ring)
    const int number = ring.fd();
    ASSERT_EQ(syscall(SYS_close_range, number, number, 0), 0);
    ring.forget();

    // A CAPIO file with the number of the ring: open takes the lowest number free, else dup2
    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    if (fd != number) {
        ASSERT_EQ(dup2(fd, number), number);
        EXPECT_EQ(close(fd), 0);
    }
    EXPECT_TRUE(closes_as_a_capio_file(number));
    EXPECT_EQ(unlink(PATHNAME), 0);
}

TEST(SystemCallTest, TestIoUringARingReplacedByDup2FreesItsNumber) {
    constexpr const char *PATHNAME = "test_io_uring_dup2.txt";
    Ring ring;
    SKIP_WITHOUT_IO_URING(ring)
    const int number = ring.fd();

    const int fd = open(PATHNAME, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    ASSERT_NE(fd, -1);
    ASSERT_EQ(dup2(fd, number), number); // closes the ring
    ring.forget();
    EXPECT_TRUE(closes_as_a_capio_file(number));
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink(PATHNAME), 0);
}

#include <gtest/gtest.h>

#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

TEST(SystemCallTest, TestFchmod) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fchmod(fd, S_IRWXU), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFchown) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fchown(fd, getuid(), getgid()), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFgetxattr) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    uid_t tmp;
    // WARNING: CAPIO does not support yet this system call
    //  but it needs to be intercepted nevertheless. It will always return -1 as of now.
    EXPECT_EQ(fgetxattr(fd, "system.posix_acl_access", &tmp, sizeof(uid_t)), -1);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestStatfs) {
    struct statfs buf;
    EXPECT_NE(statfs(".", &buf), -1);
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_NE(fstatfs(fd, &buf), -1);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlPipeSizeOnAFileFailsWithEbadf) {
    // uutils cp asks for a bigger pipe on its destination, and goes on when the kernel says no.
    auto fd = open("test.txt", O_CREAT | O_WRONLY, 0777);
    EXPECT_EQ(fcntl(fd, F_SETPIPE_SZ, 1 << 20), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(fcntl(fd, F_GETPIPE_SZ), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlDupfdHonoursTheMinimumAndSharesTheOffset) {
    auto fd     = open("test.txt", O_CREAT | O_RDWR, 0777);
    auto dup_fd = fcntl(fd, F_DUPFD, 100);
    EXPECT_GE(dup_fd, 100);
    EXPECT_EQ(fcntl(dup_fd, F_GETFD), 0);
    EXPECT_EQ(write(fd, "abcd", 4), 4);
    EXPECT_EQ(lseek(dup_fd, 0, SEEK_CUR), 4);

    auto cloexec_fd = fcntl(fd, F_DUPFD_CLOEXEC, 200);
    EXPECT_GE(cloexec_fd, 200);
    EXPECT_EQ(fcntl(cloexec_fd, F_GETFD), FD_CLOEXEC);

    EXPECT_EQ(close(cloexec_fd), 0);
    EXPECT_EQ(close(dup_fd), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlSetfdTakesOnlyCloexec) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fcntl(fd, F_SETFD, FD_CLOEXEC), 0);
    EXPECT_EQ(fcntl(fd, F_GETFD), FD_CLOEXEC);
    EXPECT_EQ(fcntl(fd, F_SETFD, 0), 0);
    EXPECT_EQ(fcntl(fd, F_GETFD), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlSetflKeepsTheAccessMode) {
    auto fd = open("test.txt", O_CREAT | O_WRONLY, 0777);
    EXPECT_EQ(fcntl(fd, F_SETFL, O_NONBLOCK | O_RDWR), 0);
    auto flags = fcntl(fd, F_GETFL);
    EXPECT_EQ(flags & O_ACCMODE, O_WRONLY);
    EXPECT_EQ(flags & O_NONBLOCK, O_NONBLOCK);
    EXPECT_EQ(fcntl(fd, F_SETFL, 0), 0);
    EXPECT_EQ(fcntl(fd, F_GETFL) & O_NONBLOCK, 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlLocksAreGranted) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    struct flock lock{};
    lock.l_type   = F_WRLCK;
    lock.l_whence = SEEK_SET;
    EXPECT_EQ(fcntl(fd, F_SETLK, &lock), 0);
    EXPECT_EQ(fcntl(fd, F_SETLKW, &lock), 0);
    EXPECT_EQ(fcntl(fd, F_GETLK, &lock), 0);
    EXPECT_EQ(lock.l_type, F_UNLCK);
    lock.l_type = F_UNLCK;
    EXPECT_EQ(fcntl(fd, F_SETLK, &lock), 0);
    lock.l_type = F_UNLCK;
    EXPECT_EQ(fcntl(fd, F_GETLK, &lock), -1);
    EXPECT_EQ(errno, EINVAL);
    lock.l_type = 42;
    EXPECT_EQ(fcntl(fd, F_SETLK, &lock), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlLockNeedsTheMatchingAccessMode) {
    auto fd = open("test.txt", O_CREAT | O_RDONLY, 0777);
    struct flock lock{};
    lock.l_type   = F_WRLCK;
    lock.l_whence = SEEK_SET;
    EXPECT_EQ(fcntl(fd, F_SETLK, &lock), -1);
    EXPECT_EQ(errno, EBADF);
    lock.l_type = F_RDLCK;
    EXPECT_EQ(fcntl(fd, F_SETLK, &lock), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlOpenFileDescriptionLocks) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    struct flock lock{};
    lock.l_type   = F_WRLCK;
    lock.l_whence = SEEK_SET;
    EXPECT_EQ(fcntl(fd, F_OFD_SETLK, &lock), 0);
    EXPECT_EQ(fcntl(fd, F_OFD_GETLK, &lock), 0);
    EXPECT_EQ(lock.l_type, F_UNLCK);
    lock.l_type = F_WRLCK;
    lock.l_pid  = getpid();
    EXPECT_EQ(fcntl(fd, F_OFD_SETLK, &lock), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlOwnerAndSignal) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fcntl(fd, F_SETOWN, getpid()), 0);
    EXPECT_EQ(fcntl(fd, F_GETOWN), getpid());
    EXPECT_EQ(fcntl(fd, F_SETSIG, SIGUSR1), 0);
    EXPECT_EQ(fcntl(fd, F_GETSIG), SIGUSR1);
    struct f_owner_ex owner{};
    EXPECT_EQ(fcntl(fd, F_GETOWN_EX, &owner), 0);
    EXPECT_EQ(owner.pid, getpid());
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlLeaseIsNeverGranted) {
    auto fd = open("test.txt", O_CREAT | O_RDONLY, 0777);
    EXPECT_EQ(fcntl(fd, F_GETLEASE), F_UNLCK);
    EXPECT_EQ(fcntl(fd, F_SETLEASE, F_RDLCK), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fcntl(fd, F_SETLEASE, F_UNLCK), 0);
    EXPECT_EQ(fcntl(fd, F_SETLEASE, 42), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlNotifyOnAFileFailsWithEnotdir) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fcntl(fd, F_NOTIFY, DN_MODIFY), -1);
    EXPECT_EQ(errno, ENOTDIR);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

#ifdef F_DUPFD_QUERY
TEST(SystemCallTest, TestFcntlDupfdQuery) {
    auto fd     = open("test.txt", O_CREAT | O_RDWR, 0777);
    auto other  = open("test.txt", O_RDWR);
    auto dup_fd = dup(fd);
    EXPECT_EQ(fcntl(fd, F_DUPFD_QUERY, dup_fd), 1);
    EXPECT_EQ(fcntl(fd, F_DUPFD_QUERY, other), 0);
    EXPECT_EQ(fcntl(fd, F_DUPFD_QUERY, 1000), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(close(dup_fd), 0);
    EXPECT_EQ(close(other), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}
#endif

TEST(SystemCallTest, TestFcntlSealsFailWithEinval) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fcntl(fd, F_GET_SEALS), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlReadWriteHintIsSharedByTheFile) {
    auto fd       = open("test.txt", O_CREAT | O_RDWR, 0777);
    auto other    = open("test.txt", O_RDONLY);
    uint64_t hint = RWH_WRITE_LIFE_SHORT;
    EXPECT_EQ(fcntl(fd, F_SET_RW_HINT, &hint), 0);
    hint = RWH_WRITE_LIFE_NOT_SET;
    EXPECT_EQ(fcntl(other, F_GET_RW_HINT, &hint), 0);
    EXPECT_EQ(hint, RWH_WRITE_LIFE_SHORT);
    hint = RWH_WRITE_LIFE_EXTREME + 1;
    EXPECT_EQ(fcntl(fd, F_SET_RW_HINT, &hint), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(other), 0);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

TEST(SystemCallTest, TestFcntlUnknownCommandFailsWithEinval) {
    auto fd = open("test.txt", O_CREAT | O_RDWR, 0777);
    EXPECT_EQ(fcntl(fd, 9999), -1);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(close(fd), 0);
    EXPECT_EQ(unlink("test.txt"), 0);
}

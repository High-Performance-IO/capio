#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

#include "child.hpp"
#include "common/env.hpp"
#include "common/semaphore.hpp"
#include "common/shm.hpp"

namespace {

/// @brief A name for the shared memory or semaphore of a test, unique to this process
std::string unique_name(const std::string &what) {
    static int count = 0;
    return "/capio_common_test_" + what + "_" + std::to_string(getpid()) + "_" +
           std::to_string(count++);
}

/// @brief Runs @p wait on this thread while another one sends it a signal, whose handler does
/// not restart system calls, and then calls @p release
template <typename Wait, typename Release> void interrupted(Wait wait, Release release) {
    struct sigaction action{};
    action.sa_handler = [](int) {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0; // no SA_RESTART: sem_wait fails with EINTR
    sigaction(SIGUSR1, &action, nullptr);

    const pthread_t waiter = pthread_self();
    std::thread signaler([waiter, release] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pthread_kill(waiter, SIGUSR1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        release();
    });
    wait();
    signaler.join();
}

} // namespace

TEST(SemaphoreTest, ANamedSemaphoreCountsAndIsRemovedWithItsObject) {
    const std::string name = unique_name("sem");
    {
        NamedSemaphore semaphore(name, 1);
        semaphore.lock();
        semaphore.unlock();
        semaphore.lock();
    }
    EXPECT_EQ(sem_open(name.c_str(), 0), SEM_FAILED); // unlinked
    EXPECT_EQ(errno, ENOENT);
}

TEST(SemaphoreTest, InvalidSemaphoresAreRefused) {
    EXPECT_THROW(NamedSemaphore("/no/slashes/inside", 0), std::runtime_error);
    EXPECT_THROW(Semaphore(static_cast<unsigned>(SEM_VALUE_MAX) + 1), std::runtime_error);

    NamedSemaphore named(unique_name("sem"), SEM_VALUE_MAX);
    EXPECT_THROW(named.unlock(), std::runtime_error); // past the largest value
    Semaphore unnamed(SEM_VALUE_MAX);
    EXPECT_THROW(unnamed.unlock(), std::runtime_error);
}

/*
 * A signal handler interrupts sem_wait even with SA_RESTART: a CAPIO thread waiting on a
 * semaphore must go on waiting, not fail. In children, as the handler is for the whole process.
 */
TEST(SemaphoreTest, ALockInterruptedByASignalWaitsAgain) {
    EXPECT_EQ(run_in_child([] {
                  Semaphore semaphore(0);
                  std::atomic_bool released = false;
                  interrupted([&] { EXPECT_NO_THROW(semaphore.lock()); },
                              [&] {
                                  released = true;
                                  semaphore.unlock();
                              });
                  EXPECT_TRUE(released);
              }),
              0);
    EXPECT_EQ(run_in_child([] {
                  NamedSemaphore semaphore(unique_name("sem"), 0);
                  std::atomic_bool released = false;
                  interrupted([&] { EXPECT_NO_THROW(semaphore.lock()); },
                              [&] {
                                  released = true;
                                  semaphore.unlock();
                              });
                  EXPECT_TRUE(released);
              }),
              0);
}

TEST(SemaphoreTest, ASemaphoreAlreadyUnlinkedEndsTheProcess) {
    // Two objects of one name: the second cannot unlink it again, which ends the process
    EXPECT_EQ(run_in_child([] {
                  const std::string name = unique_name("sem");
                  auto *first            = new NamedSemaphore(name, 0);
                  auto *second           = new NamedSemaphore(name, 0);
                  delete first;
                  delete second; // exits
                  _exit(0);
              }),
              EXIT_FAILURE);
}

TEST(NoLockTest, LocksNothing) {
    NoLock lock("unused", 0);
    NoLock::lock();
    NoLock::unlock();
}

TEST(SharedMemoryTest, ACreatedSegmentIsFoundByName) {
    const std::string name = unique_name("shm");
    auto *created          = static_cast<int *>(create_shm(name, sizeof(int)));
    *created               = 42;
    auto *found            = static_cast<int *>(get_shm(name));
    EXPECT_EQ(*found, 42);
    auto *if_exists = static_cast<int *>(get_shm_if_exist(name));
    ASSERT_NE(if_exists, nullptr);
    EXPECT_EQ(*if_exists, 42);
    munmap(created, sizeof(int));
    munmap(found, sizeof(int));
    munmap(if_exists, sizeof(int));
    shm_unlink(name.c_str());

    EXPECT_EQ(get_shm_if_exist(name), nullptr);
    EXPECT_THROW(get_shm(name), std::runtime_error);
}

TEST(SharedMemoryTest, SegmentsThatCannotBeMadeOrMappedAreRefused) {
    EXPECT_THROW(create_shm("/no/slashes/inside", 8), std::runtime_error);
    EXPECT_THROW(get_shm_if_exist("/no/slashes/inside"), std::runtime_error);

    const std::string negative = unique_name("shm");
    EXPECT_THROW(create_shm(negative, -1), std::runtime_error); // ftruncate
    shm_unlink(negative.c_str());

    const std::string empty = unique_name("shm");
    EXPECT_THROW(create_shm(empty, 0), std::runtime_error); // mmap of no bytes
    // The segment exists, with no bytes: it cannot be mapped either
    EXPECT_THROW(get_shm(empty), std::runtime_error);
    EXPECT_THROW(get_shm_if_exist(empty), std::runtime_error);
    shm_unlink(empty.c_str());
}

/*
 * The settings are read once per process, so each case runs in a child that has not read them.
 */
TEST(EnvironmentTest, TheCacheSettingsAndTheWorkflowNameComeFromTheEnvironment) {
    EXPECT_EQ(run_in_child([] {
                  setenv("CAPIO_CACHE_LINES", "7", 1);
                  setenv("CAPIO_CACHE_LINE_SIZE", "4096", 1);
                  setenv("CAPIO_WORKFLOW_NAME", "a_workflow", 1);
                  EXPECT_EQ(get_cache_lines(), 7);
                  EXPECT_EQ(get_cache_line_size(), 4096);
                  EXPECT_EQ(get_capio_workflow_name(), "a_workflow");
                  setenv("CAPIO_CACHE_LINES", "8", 1); // read once: no change
                  EXPECT_EQ(get_cache_lines(), 7);
              }),
              0);
    EXPECT_EQ(run_in_child([] {
                  unsetenv("CAPIO_CACHE_LINES");
                  setenv("CAPIO_CACHE_LINE_SIZE", "-3", 1); // not a size: the default
                  unsetenv("CAPIO_WORKFLOW_NAME");
                  EXPECT_EQ(get_cache_lines(), CAPIO_CACHE_LINES_DEFAULT);
                  EXPECT_EQ(get_cache_line_size(), CAPIO_CACHE_LINE_SIZE_DEFAULT);
                  EXPECT_EQ(get_capio_workflow_name(), CAPIO_DEFAULT_WORKFLOW_NAME);
              }),
              0);
}

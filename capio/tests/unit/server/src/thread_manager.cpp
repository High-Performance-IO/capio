#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <future>
#include <pthread.h>
#include <thread>

#include "utils/thread_manager.hpp"

namespace {

using namespace std::chrono_literals;

/// @brief Wait, at most 5s, for every thread of @p manager to end: a manager must outlive them
bool ends(const ThreadManager &manager) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!manager.running().empty()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST(ThreadManagerTest, AThreadDoesNotGetTheTerminationSignals) {
    ThreadManager manager;
    std::promise<sigset_t> thread_mask;
    ASSERT_TRUE(manager.startWaiter("the mask", [&thread_mask] {
        sigset_t mask;
        pthread_sigmask(SIG_BLOCK, nullptr, &mask);
        thread_mask.set_value(mask);
    }));
    const sigset_t mask = thread_mask.get_future().get();
    for (const int signal : {SIGTERM, SIGINT, SIGQUIT, SIGPIPE}) {
        EXPECT_EQ(sigismember(&mask, signal), 1) << strsignal(signal);
    }

    // The caller gets them again
    sigset_t caller;
    pthread_sigmask(SIG_BLOCK, nullptr, &caller);
    EXPECT_EQ(sigismember(&caller, SIGTERM), 0);
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, AThreadGetsItsArguments) {
    ThreadManager manager;
    std::promise<int> sum;
    int by_reference = 0;
    ASSERT_TRUE(manager.startTask(
        "a sum",
        [](std::promise<int> *result, const int a, const int b, int &reference) {
            reference = a;
            result->set_value(a + b);
        },
        &sum, 40, 2, std::ref(by_reference)));
    EXPECT_EQ(sum.get_future().get(), 42);
    EXPECT_TRUE(ends(manager));
    EXPECT_EQ(by_reference, 40); // std::ref passes a reference, as with std::thread
}

TEST(ThreadManagerTest, AThreadIsKnownUntilItEnds) {
    ThreadManager manager;
    std::promise<void> release;
    ASSERT_TRUE(manager.startWaiter("open of /f by 1: its creation",
                                    [ready = release.get_future().share()] { ready.wait(); }));
    const auto running = manager.running();
    ASSERT_EQ(running.size(), 1);
    EXPECT_EQ(running[0].kind, ThreadManager::Kind::WAITER);
    EXPECT_EQ(running[0].name, "open of /f by 1: its creation");

    release.set_value();
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, StopWaitsForTheTasks) {
    ThreadManager manager;
    std::atomic<bool> done = false;
    ASSERT_TRUE(manager.startTask("a slow task", [&done] {
        std::this_thread::sleep_for(100ms);
        done = true;
    }));
    EXPECT_TRUE(manager.stop(5s));
    EXPECT_TRUE(done);
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, StopDoesNotWaitForTheWaiters) {
    ThreadManager manager;
    std::promise<void> release;
    ASSERT_TRUE(manager.startWaiter("a file nobody produces",
                                    [ready = release.get_future().share()] { ready.wait(); }));
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(manager.stop(5s));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    EXPECT_EQ(manager.running().size(), 1); // logged, still running

    release.set_value();
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, StopGivesUpOnATaskAfterItsTimeout) {
    ThreadManager manager;
    std::promise<void> release;
    ASSERT_TRUE(manager.startTask("a stuck task",
                                  [ready = release.get_future().share()] { ready.wait(); }));
    EXPECT_FALSE(manager.stop(100ms));

    release.set_value();
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, NoThreadStartsAfterStop) {
    ThreadManager manager;
    EXPECT_TRUE(manager.stop(0ms));
    std::atomic<bool> ran = false;
    EXPECT_FALSE(manager.startWaiter("too late", [&ran] { ran = true; }));
    EXPECT_FALSE(manager.startTask("too late", [&ran] { ran = true; }));
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(ran);
    EXPECT_TRUE(manager.running().empty());
}

TEST(ThreadManagerTest, ATaskThatStopsTheManagerDoesNotWaitForItself) {
    // A fault in a task runs the teardown on that task
    ThreadManager manager;
    std::promise<std::pair<bool, std::chrono::steady_clock::duration>> stopped;
    ASSERT_TRUE(manager.startTask("a faulty task", [&manager, &stopped] {
        const auto start = std::chrono::steady_clock::now();
        const bool all   = manager.stop(5s);
        stopped.set_value({all, std::chrono::steady_clock::now() - start});
    }));
    const auto [all, took] = stopped.get_future().get();
    EXPECT_TRUE(all);
    EXPECT_LT(took, 1s);
    EXPECT_TRUE(ends(manager));
}

TEST(ThreadManagerTest, TheServerHasOne) { EXPECT_EQ(&thread_manager(), &thread_manager()); }

TEST(ThreadManagerTest, AWaiterIsNamedAfterItsRequest) {
    EXPECT_EQ(thread_name("open", "/a/b", 1234, "its creation"),
              "open of /a/b by 1234: its creation");
    EXPECT_EQ(thread_name("read", "/a/b", std::string("node1"), "its data"),
              "read of /a/b for node1: its data");
}

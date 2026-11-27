#include <gtest/gtest.h>

#include <array>
#include <deque>
#include <stdexcept>
#include <string>
#include <typeinfo>

#include "child.hpp"
#include "remote/listener.hpp"
#include "storage/file_buffer.hpp"
#include "test_backend.hpp"

namespace {

/// @brief Sets the configuration of the server to the one of @p arguments
void configure(std::vector<std::string> arguments) {
    std::vector<char *> argv;
    for (auto &argument : arguments) {
        argv.push_back(argument.data());
    }
    RuntimeConfiguration::set(RuntimeConfiguration(static_cast<int>(argv.size()), argv.data()));
}

/// @brief Hands out the requests of @ref script, then throws to end the listener's loop
class ScriptedBackend : public RecordingBackend {
  public:
    std::deque<std::string> script;
    RemoteRequest read_next_request() override {
        if (script.empty()) {
            throw std::runtime_error("end of the script");
        }
        std::string next = script.front();
        script.pop_front();
        return {next, std::string("peer")};
    }
};

} // namespace

/*
 * Each in a child: select_backend reads the global configuration, and an MPI or MTCL backend can be
 * made once in a process.
 */
TEST(SelectBackendTest, TheConfigurationNamesTheBackend) {
    for (const auto &[name, type] : std::vector<std::pair<std::string, std::string>>{
             {"", typeid(NoneBackend).name()},
             {"none", typeid(NoneBackend).name()},
             {"no-such-backend", typeid(NoneBackend).name()},
             {"mtcl", typeid(MTCLBackend).name()},
#ifdef CAPIO_HAS_MPI
             {"mpi", typeid(MPIBackend).name()},
             {"mpisync", typeid(MPISYNCBackend).name()},
#endif
         }) {
        EXPECT_EQ(run_in_child([&name, &type] {
                      std::vector<std::string> arguments{"capio_server", "--no-config"};
                      if (!name.empty()) {
                          arguments.insert(arguments.end(), {"--backend", name});
                      }
                      if (name == "mtcl") { // a port no other test uses
                          arguments.insert(arguments.end(), {"--backend-options", "TCP:7699@1000"});
                      }
                      configure(arguments);
                      std::array<char *, 2> argv{const_cast<char *>("capio_server"), nullptr};
                      Backend *selected = select_backend(1, argv.data());
                      EXPECT_EQ(typeid(*selected).name(), type);
                      delete selected;
                  }),
                  0)
            << name;
    }
}

TEST(RemoteListenerTest, ANoneBackendHasNoListener) {
    NoneBackend none(0, nullptr);
    ScopedTestBackend scoped(&none);
    Semaphore started(1);
    capio_remote_listener(started); // returns at once: no other server can send anything
}

TEST(RemoteListenerTest, RequestsGoToTheirHandlersAndInvalidOnesAreIgnored) {
    ScriptedBackend scripted;
    ScopedTestBackend scoped(&scripted);
    scripted.script = {"9999 a code past the last one", "abcd not a code",
                       "0006 /listener/announced"};
    Semaphore started(1);
    EXPECT_THROW(capio_remote_listener(started), std::runtime_error);
    // The announce made the sender the home of the file
    EXPECT_EQ(home_node_service->getHome("/listener/announced"),
              std::optional<std::string>("peer"));
    home_node_service->forget("/listener/announced", "peer");
}

TEST(StorageAllocatorTest, ARequestLargerThanTheMemoryIsCapped) {
    const off64_t huge = std::numeric_limits<off64_t>::max();
    const off64_t got  = computeAllocableMemQuota(huge);
    EXPECT_GT(got, 0);
    EXPECT_LT(got, huge);
    EXPECT_EQ(computeAllocableMemQuota(4096), 4096);
    EXPECT_EQ(computeAllocableMemQuota(-1), -1); // not a size: returned as it is
}

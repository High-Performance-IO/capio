#include <gtest/gtest.h>

#include "common/constants.hpp"
#include "utils/runtime_configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

class RuntimeConfigurationTest : public testing::Test {
  protected:
    std::map<std::string, std::optional<std::string>> saved_environment;

    void SetUp() override {
        for (const auto env : {"CAPIO_DIR", "CAPIO_CACHE_LINES", "CAPIO_CACHE_LINE_SIZE",
                               "CAPIO_FILE_INIT_SIZE", "CAPIO_PREFETCH_DATA_SIZE"}) {
            const char *value      = std::getenv(env);
            saved_environment[env] = value ? std::optional<std::string>{value} : std::nullopt;
            unsetenv(env);
        }
        setenv("CAPIO_DIR", "/tmp", 1);
    }

    void TearDown() override {
        for (const auto &[env, value] : saved_environment) {
            value ? setenv(env.c_str(), value->c_str(), 1) : unsetenv(env.c_str());
        }
    }

    static RuntimeConfiguration parse(std::vector<std::string> arguments) {
        std::vector<char *> argv;
        argv.reserve(arguments.size());
        for (auto &argument : arguments) {
            argv.push_back(argument.data());
        }
        return {static_cast<int>(argv.size()), argv.data()};
    }
};

} // namespace

TEST_F(RuntimeConfigurationTest, UsesDefaultsWithNoConfig) {
    const auto config = parse({"capio_server", "--no-config"});

    EXPECT_EQ(config.getCapioDir(), std::filesystem::canonical("/tmp"));
    EXPECT_FALSE(config.getContinueOnError());
    EXPECT_EQ(config.getDiscoveryType(), CAPIO_MCAST_PROTO_FLAG);
    EXPECT_EQ(config.getMulticastDiscoveryAddress(), CAPIO_MCAST_ADV_DEFAULT_ADDR);
    EXPECT_EQ(config.getMulticastDiscoveryPort(), CAPIO_MCAST_ADV_DEFAULT_PORT);
    EXPECT_EQ(config.getFsDiscoveryTokenDirectory(), CAPIO_FS_DEFAULT_TOKEN_DIRECTORY);
    EXPECT_TRUE(config.getBackendName().empty());
    EXPECT_EQ(config.getMtclProtocol(), CAPIO_MTCL_DEFAULT_PROTOCOL);
    EXPECT_EQ(config.getMtclPort(), CAPIO_MTCL_DEFAULT_PORT);
    EXPECT_EQ(config.getMtclPollInterval(), CAPIO_MTCL_DEFAULT_POLL_INTERVAL);
    EXPECT_EQ(config.getCacheLines(), CAPIO_CACHE_LINES_DEFAULT);
    EXPECT_EQ(config.getCacheLineSize(), CAPIO_CACHE_LINE_SIZE_DEFAULT);
    EXPECT_EQ(config.getFileInitialSize(), CAPIO_DEFAULT_FILE_INITIAL_SIZE);
    EXPECT_EQ(config.getPrefetchDataSize(), 0);
}

TEST_F(RuntimeConfigurationTest, ParsesExplicitOptions) {
    const auto config =
        parse({"capio_server", "--no-config", "--backend", "mtcl", "--backend-options",
               "TCP:7601@50", "--discovery", "fs", "--mcast-addr", "239.1.2.3", "--mcast-port",
               "12345", "--token-directory", "/tokens", "--continue-on-error"});

    EXPECT_EQ(config.getBackendName(), "mtcl");
    EXPECT_EQ(config.getMtclProtocol(), "TCP");
    EXPECT_EQ(config.getMtclPort(), 7601);
    EXPECT_EQ(config.getMtclPollInterval(), 50);
    EXPECT_EQ(config.getDiscoveryType(), CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.getMulticastDiscoveryAddress(), "239.1.2.3");
    EXPECT_EQ(config.getMulticastDiscoveryPort(), 12345);
    EXPECT_EQ(config.getFsDiscoveryTokenDirectory(), "/tokens");
    EXPECT_TRUE(config.getContinueOnError());
}

TEST_F(RuntimeConfigurationTest, ReadsEnvironment) {
    setenv("CAPIO_CACHE_LINES", "20", 1);
    setenv("CAPIO_CACHE_LINE_SIZE", "8192", 1);
    setenv("CAPIO_FILE_INIT_SIZE", "4096", 1);
    setenv("CAPIO_PREFETCH_DATA_SIZE", "1024", 1);

    const auto config = parse({"capio_server", "--no-config"});

    EXPECT_EQ(config.getCacheLines(), 20);
    EXPECT_EQ(config.getCacheLineSize(), 8192);
    EXPECT_EQ(config.getFileInitialSize(), 4096);
    EXPECT_EQ(config.getPrefetchDataSize(), 1024);
}

TEST_F(RuntimeConfigurationTest, CreatesCapioClEngineWithoutConfigFile) {
    const auto config = parse({"capio_server", "--no-config", "--mem-only"});
    const auto engine = config.createCapioClEngine();
    EXPECT_EQ(engine->getWorkflowName(), CAPIO_DEFAULT_WORKFLOW_NAME);
    delete engine;
}

TEST_F(RuntimeConfigurationTest, RejectsInvalidConfigurations) {
    EXPECT_THROW(parse({"capio_server"}), std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--unknown"}), std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--discovery", "dns"}), std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--backend", "mtcl", "--backend-options",
                        "TCP:7601"}),
                 std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--backend", "mtcl", "--backend-options",
                        "TCP:port@50"}),
                 std::runtime_error);

    unsetenv("CAPIO_DIR");
    EXPECT_THROW(parse({"capio_server", "--no-config"}), std::runtime_error);
    setenv("CAPIO_DIR", "/does/not/exist", 1);
    EXPECT_THROW(parse({"capio_server", "--no-config"}), std::runtime_error);
    setenv("CAPIO_DIR", "/proc/self", 1);
    EXPECT_THROW(parse({"capio_server", "--no-config"}), std::runtime_error);
}

#include <gtest/gtest.h>

#include "common/constants.hpp"
#include "utils/runtime_configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

class RuntimeConfigurationTest : public testing::Test {
  protected:
    std::map<std::string, std::optional<std::string>> saved_environment;
    std::filesystem::path working_dir;

    void SetUp() override {
        for (const auto env : {"CAPIO_CACHE_LINES", "CAPIO_CACHE_LINE_SIZE", "CAPIO_FILE_INIT_SIZE",
                               "CAPIO_PREFETCH_DATA_SIZE"}) {
            const char *value      = std::getenv(env);
            saved_environment[env] = value ? std::optional<std::string>{value} : std::nullopt;
            unsetenv(env);
        }
        working_dir = std::filesystem::current_path();
    }

    void TearDown() override {
        std::filesystem::current_path(working_dir);
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

    EXPECT_TRUE(config.getRoot().empty());
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
    EXPECT_EQ(config.getLocationQueryTimeout().count(), CAPIO_LOCATION_QUERY_TIMEOUT_DEFAULT_MS);
    EXPECT_EQ(config.getLocationRequeryInterval().count(),
              CAPIO_LOCATION_REQUERY_INTERVAL_DEFAULT_MS);
}

TEST_F(RuntimeConfigurationTest, ParsesExplicitOptions) {
    const auto config =
        parse({"capio_server", "--no-config", "--backend", "mtcl", "--backend-options",
               "TCP:7601@50", "--discovery", "fs", "--mcast-addr", "239.1.2.3", "--mcast-port",
               "12345", "--token-directory", "/tokens", "--continue-on-error",
               "--location-query-timeout", "750", "--location-requery-interval", "25"});

    EXPECT_EQ(config.getBackendName(), "mtcl");
    EXPECT_EQ(config.getMtclProtocol(), "TCP");
    EXPECT_EQ(config.getMtclPort(), 7601);
    EXPECT_EQ(config.getMtclPollInterval(), 50);
    EXPECT_EQ(config.getDiscoveryType(), CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.getMulticastDiscoveryAddress(), "239.1.2.3");
    EXPECT_EQ(config.getMulticastDiscoveryPort(), 12345);
    EXPECT_EQ(config.getFsDiscoveryTokenDirectory(), "/tokens");
    EXPECT_TRUE(config.getContinueOnError());
    EXPECT_EQ(config.getLocationQueryTimeout(), std::chrono::milliseconds(750));
    EXPECT_EQ(config.getLocationRequeryInterval(), std::chrono::milliseconds(25));
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

TEST_F(RuntimeConfigurationTest, CreatedEnginesCountClosesAndLeaveTokensUnderTheRoot) {
    char dir[] = "/tmp/capio_close_count_XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    const auto root = std::filesystem::canonical(dir);
    std::ofstream(root / "config.json") << R"({"name": "CAPIO", "IO_Graph": []})";

    const std::vector<std::vector<std::string>> servers = {
        {"capio_server", "--no-config", "-r", root.string()},
        {"capio_server", "-c", (root / "config.json").string(), "-r", root.string()}};
    for (size_t i = 0; i < servers.size(); ++i) {
        const auto config = parse(servers[i]);
        EXPECT_EQ(config.getRoot(), root);
        const std::unique_ptr<capiocl::engine::Engine> engine(config.createCapioClEngine());
        // A virtual path: "/" is the clients' CAPIO_DIR, whose real directory is the root
        const std::filesystem::path file = "/file" + std::to_string(i);
        engine->setCommitRule(file, capiocl::commitRules::ON_CLOSE);
        engine->setCommitedCloseNumber(file, 2);

        EXPECT_FALSE(engine->increaseCloseCount(file));
        EXPECT_TRUE(engine->increaseCloseCount(file));
        // Found by servers that start later; the counters are under the root, the default
        EXPECT_TRUE(std::filesystem::exists(root / (".file" + std::to_string(i) + ".commit")));
        EXPECT_TRUE(std::filesystem::is_directory(root / "capiocl" / "close-counts"));
    }
    std::filesystem::remove_all(root);
}

TEST_F(RuntimeConfigurationTest, WithoutRootNoTokensAreWritten) {
    char dir[] = "/tmp/capio_no_root_XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    const auto working_dir = std::filesystem::canonical(dir);
    std::filesystem::current_path(working_dir);

    const auto config = parse({"capio_server", "--no-config"});
    EXPECT_TRUE(config.getRoot().empty());
    const std::unique_ptr<capiocl::engine::Engine> engine(config.createCapioClEngine());
    engine->setCommitted("/f");
    EXPECT_TRUE(engine->isCommitted("/f")); // by multicast
    EXPECT_TRUE(std::filesystem::is_empty(working_dir));
    std::filesystem::remove_all(working_dir);
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
    EXPECT_THROW(parse({"capio_server", "--no-config", "--location-query-timeout", "0"}),
                 std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--location-requery-interval", "-5"}),
                 std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "--location-query-timeout", "soon"}),
                 std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "-r", "/does/not/exist"}),
                 std::runtime_error);
    EXPECT_THROW(parse({"capio_server", "--no-config", "-r", "/proc/self/status"}),
                 std::runtime_error);
}

TEST_F(RuntimeConfigurationTest, NamesInTheConfigurationAreVirtual) {
    char dir[] = "/tmp/capio_relative_XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    const auto root = std::filesystem::canonical(dir);
    std::ofstream(root / "config.json")
        << R"({"name": "CAPIO", "IO_Graph": [)"
        << R"({"name": "writer", "input_stream": [], "output_stream": ["out.dat", "/d/abs.dat"],)"
        << R"( "streaming": [{"name": ["out.dat"], "committed": "on_close"}]},)"
        << R"({"name": "reader", "input_stream": ["out.dat"], "output_stream": []}]})";

    // Wherever the server runs and whatever -r says, a name is a path inside CAPIO_DIR
    for (const auto &extra : std::vector<std::vector<std::string>>{{}, {"-r", root.string()}}) {
        std::vector<std::string> arguments{"capio_server", "-c", (root / "config.json").string()};
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        const std::unique_ptr<capiocl::engine::Engine> engine(
            parse(arguments).createCapioClEngine());
        EXPECT_EQ(engine->getCommitRule("/out.dat"), capiocl::commitRules::ON_CLOSE);
        EXPECT_TRUE(engine->isConsumer("/out.dat", "reader"));
        EXPECT_TRUE(engine->isProducer("/d/abs.dat", "writer"));
        EXPECT_FALSE(engine->isConsumer(root / "out.dat", "reader"));
    }
    std::filesystem::remove_all(root);
}

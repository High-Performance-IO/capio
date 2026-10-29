#include <gtest/gtest.h>

#include "common/constants.hpp"
#include "utils/runtime_configuration.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>

namespace {

class RuntimeConfigurationTest : public testing::Test {
  protected:
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "capio-runtime-config-test.toml";

    std::map<std::string, std::optional<std::string>> saved_environment;

    void SetUp() override {
        for (const auto env : {"CAPIO_DIR", "CAPIO_CACHE_LINES", "CAPIO_CACHE_LINE_SIZE",
                               "CAPIO_FILE_INIT_SIZE", "CAPIO_PREFETCH_DATA_SIZE"}) {
            const char *value      = std::getenv(env);
            saved_environment[env] = value ? std::optional<std::string>{value} : std::nullopt;
            unsetenv(env);
        }
    }

    void TearDown() override {
        std::filesystem::remove(path);
        for (const auto &[env, value] : saved_environment) {
            value ? setenv(env.c_str(), value->c_str(), 1) : unsetenv(env.c_str());
        }
    }

    const std::filesystem::path &write(const std::string &content) const {
        std::ofstream(path) << content;
        return path;
    }

    static std::string capio_cl(const RuntimeConfiguration &config, const std::string &key) {
        std::string value;
        config.getCapioClConfiguration().getParameter(key, &value, "<unset>");
        return value;
    }
};

} // namespace

TEST_F(RuntimeConfigurationTest, UsesDefaultsWithoutFile) {
    const auto config = RuntimeConfiguration(std::nullopt);

    EXPECT_EQ(config.getCapioDir(), std::filesystem::current_path());
    EXPECT_EQ(config.getDiscoveryType(), CAPIO_MCAST_PROTO_FLAG);
    EXPECT_EQ(config.getMulticastDiscoveryAddress(), CAPIO_MCAST_ADV_DEFAULT_ADDR);
    EXPECT_EQ(config.getMulticastDiscoveryPort(), CAPIO_MCAST_ADV_DEFAULT_PORT);
    EXPECT_EQ(config.getFsDiscoveryTokenDirectory(), CAPIO_FS_DEFAULT_TOKEN_DIRECTORY);
    EXPECT_EQ(config.getBackendName(), "none");
    EXPECT_EQ(config.getMtclProtocol(), CAPIO_MTCL_DEFAULT_PROTOCOL);
    EXPECT_EQ(config.getMtclPort(), CAPIO_MTCL_DEFAULT_PORT);
    EXPECT_EQ(config.getMtclPollInterval(), CAPIO_MTCL_DEFAULT_POLL_INTERVAL);
    EXPECT_EQ(config.getCacheLines(), CAPIO_CACHE_LINES_DEFAULT);
    EXPECT_EQ(config.getCacheLineSize(), CAPIO_CACHE_LINE_SIZE_DEFAULT);
    EXPECT_EQ(config.getFileInitialSize(), CAPIO_DEFAULT_FILE_INITIAL_SIZE);
    EXPECT_EQ(config.getPrefetchDataSize(), 0);
    EXPECT_FALSE(config.getContinueOnError());
    // CAPIO-CL keys are not forwarded unless set, so CAPIO-CL keeps its own defaults
    EXPECT_EQ(capio_cl(config, "capiocl.workflow_name"), "<unset>");
    EXPECT_EQ(capio_cl(config, "capiocl.config_path"), "<unset>");
}

TEST_F(RuntimeConfigurationTest, ParsesFile) {
    const auto config = RuntimeConfiguration(write(R"(
[capio]
directory = "/tmp"
continue_on_error = true
[capio.storage]
file_initial_size = 4096
prefetch_data_size = 1024
[capio.cache]
lines = 20
line_size = 8_192
[capio.discovery]
type = "fs"
mcast = { address = "239.1.2.3", port = 12345 }
fs.token_directory = "/tokens"
[capio.backend]
type = "mtcl"
[capio.backend.mtcl]
protocol = "UCX"
port = 7601
poll_interval_us = 50

[capiocl]
config_path = "workflow.json"
resolve_path = "prefix"
store_all_in_memory = true
[capiocl.monitor.mcast]
enabled = true
delay_ms = 10
)"));

    EXPECT_EQ(config.getCapioDir(), std::filesystem::canonical("/tmp"));
    EXPECT_TRUE(config.getContinueOnError());
    EXPECT_EQ(config.getFileInitialSize(), 4096);
    EXPECT_EQ(config.getPrefetchDataSize(), 1024);
    EXPECT_EQ(config.getCacheLines(), 20);
    EXPECT_EQ(config.getCacheLineSize(), 8192);
    EXPECT_EQ(config.getDiscoveryType(), CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.getMulticastDiscoveryAddress(), "239.1.2.3");
    EXPECT_EQ(config.getMulticastDiscoveryPort(), 12345);
    EXPECT_EQ(config.getFsDiscoveryTokenDirectory(), "/tokens");
    EXPECT_EQ(config.getBackendName(), "mtcl");
    EXPECT_EQ(config.getMtclProtocol(), "UCX");
    EXPECT_EQ(config.getMtclPort(), 7601);
    EXPECT_EQ(config.getMtclPollInterval(), 50);

    EXPECT_EQ(capio_cl(config, "capiocl.config_path"), "workflow.json");
    EXPECT_EQ(capio_cl(config, "capiocl.resolve_path"), "prefix");
    EXPECT_EQ(capio_cl(config, "capiocl.store_all_in_memory"), "true");
    EXPECT_EQ(capio_cl(config, "capiocl.monitor.mcast.enabled"), "true");
    EXPECT_EQ(capio_cl(config, "capiocl.monitor.mcast.delay_ms"), "10");
    EXPECT_EQ(capio_cl(config, "capiocl.workflow_name"), "<unset>");
}

TEST_F(RuntimeConfigurationTest, AppliesDefaultsThenEnvironmentThenFileThenOverrides) {
    setenv("CAPIO_CACHE_LINES", "30", 1);
    setenv("CAPIO_CACHE_LINE_SIZE", "1024", 1);
    setenv("CAPIO_FILE_INIT_SIZE", "2048", 1);
    write("[capio.cache]\nline_size = 4096\n[capio.storage]\nfile_initial_size = 8192\n");

    const auto config = RuntimeConfiguration(
        path, {"capio.storage.file_initial_size=16384", "capio.backend.type=mtcl",
               "capiocl.workflow_name=wf", "capiocl.dynamic_api.enabled=true"});

    EXPECT_EQ(config.getCacheLines(), 30);         // environment
    EXPECT_EQ(config.getCacheLineSize(), 4096);    // file beats environment
    EXPECT_EQ(config.getFileInitialSize(), 16384); // override beats file
    EXPECT_EQ(config.getBackendName(), "mtcl");
    EXPECT_EQ(capio_cl(config, "capiocl.workflow_name"), "wf");
    EXPECT_EQ(capio_cl(config, "capiocl.dynamic_api.enabled"), "true");
}

TEST_F(RuntimeConfigurationTest, RejectsInvalidConfigurations) {
    // Typos are reported instead of being silently ignored
    EXPECT_THROW(RuntimeConfiguration(write("[capio.cache]\nline = 3\n")), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capoi]\ndirectory = \"/tmp\"\n")),
                 std::runtime_error);
    // Wrong types
    EXPECT_THROW(RuntimeConfiguration(write("[capio.cache]\nlines = \"3\"\n")), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capio]\ncontinue_on_error = 1\n")),
                 std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capio]\ndirectory = [\"/tmp\"]\n")),
                 std::runtime_error);
    // Out of range and unknown choices
    EXPECT_THROW(RuntimeConfiguration(write("[capio.cache]\nlines = 0\n")), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capio.discovery]\ntype = \"dns\"\n")),
                 std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capio.backend.mtcl]\nport = 70000\n")),
                 std::runtime_error);
    // Missing directory and broken TOML
    EXPECT_THROW(RuntimeConfiguration(write("[capio]\ndirectory = \"/does/not/exist\"\n")),
                 std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(write("[capio\n")), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(path.string() + ".missing"), std::runtime_error);
    // Invalid overrides
    EXPECT_THROW(RuntimeConfiguration(std::nullopt, {"capio.cache.lines"}), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(std::nullopt, {"capio.cache.lines=ten"}), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(std::nullopt, {"capio.unknown=1"}), std::runtime_error);
    EXPECT_THROW(RuntimeConfiguration(std::nullopt, {"capio.continue_on_error=yes"}),
                 std::runtime_error);
    // Invalid environment
    setenv("CAPIO_CACHE_LINES", "many", 1);
    EXPECT_THROW(RuntimeConfiguration(std::nullopt), std::runtime_error);
}

TEST_F(RuntimeConfigurationTest, AcceptsUnknownBackendsToFallBackToNone) {
    EXPECT_EQ(RuntimeConfiguration(std::nullopt, {"capio.backend.type=tcp"}).getBackendName(),
              "tcp");
}

TEST_F(RuntimeConfigurationTest, ForwardsUnknownCapioClKeys) {
    const auto config = RuntimeConfiguration(write("[capiocl.future]\noption = 3\n"));
    EXPECT_EQ(capio_cl(config, "capiocl.future.option"), "3");
}

TEST_F(RuntimeConfigurationTest, TemplateDocumentsEveryOptionWithItsDefault) {
    std::ostringstream output;
    RuntimeConfiguration::writeTemplate(output);
    const auto text = output.str();

    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        EXPECT_LE(line.size(), 100) << line;
    }

    // The template as generated, with everything commented out, means "all defaults"
    write(text);
    const auto defaults = RuntimeConfiguration(std::nullopt);
    const auto as_is    = RuntimeConfiguration(path);
    EXPECT_EQ(as_is.getBackendName(), defaults.getBackendName());
    EXPECT_EQ(as_is.getCacheLines(), defaults.getCacheLines());

    // Uncommenting every option must still give a valid configuration equal to the defaults
    const std::regex commented_option(R"(^# ([a-z_]+ = .*)$)", std::regex::multiline);
    write(std::regex_replace(text, commented_option, "$1"));
    const auto uncommented = RuntimeConfiguration(path);
    EXPECT_EQ(uncommented.getCapioDir(), defaults.getCapioDir());
    EXPECT_EQ(uncommented.getDiscoveryType(), defaults.getDiscoveryType());
    EXPECT_EQ(uncommented.getMulticastDiscoveryAddress(), defaults.getMulticastDiscoveryAddress());
    EXPECT_EQ(uncommented.getMulticastDiscoveryPort(), defaults.getMulticastDiscoveryPort());
    EXPECT_EQ(uncommented.getBackendName(), defaults.getBackendName());
    EXPECT_EQ(uncommented.getMtclPort(), defaults.getMtclPort());
    EXPECT_EQ(uncommented.getFileInitialSize(), defaults.getFileInitialSize());
    EXPECT_EQ(capio_cl(uncommented, "capiocl.config_path"), "");
    EXPECT_EQ(capio_cl(uncommented, "capiocl.workflow_name"), CAPIO_DEFAULT_WORKFLOW_NAME);
}

TEST_F(RuntimeConfigurationTest, EffectiveConfigurationCanBeLoadedBack) {
    std::ostringstream output;
    RuntimeConfiguration(std::nullopt,
                         {"capio.backend.type=mtcl", "capiocl.config_path=a \"quoted\" path"})
        .write(output);
    write(output.str());

    const auto config = RuntimeConfiguration(path);
    EXPECT_EQ(config.getBackendName(), "mtcl");
    EXPECT_EQ(capio_cl(config, "capiocl.config_path"), "a \"quoted\" path");
}

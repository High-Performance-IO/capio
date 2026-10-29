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

    static std::string capio_cl(const CapioParsedConfig &config, const std::string &key) {
        std::string value;
        config.capio_cl_config.getParameter(key, &value, "<unset>");
        return value;
    }
};

} // namespace

TEST_F(RuntimeConfigurationTest, UsesDefaultsWithoutFile) {
    const auto config = load_config(std::nullopt);

    EXPECT_EQ(config.capio_dir, std::filesystem::current_path());
    EXPECT_EQ(config.discovery_interface, CAPIO_MCAST_PROTO_FLAG);
    EXPECT_EQ(config.mcast_addr, CAPIO_MCAST_ADV_DEFAULT_ADDR);
    EXPECT_EQ(config.mcast_port, CAPIO_MCAST_ADV_DEFAULT_PORT);
    EXPECT_EQ(config.token_directory, CAPIO_FS_DEFAULT_TOKEN_DIRECTORY);
    EXPECT_EQ(config.backend_name, "none");
    EXPECT_EQ(config.mtcl_protocol, CAPIO_MTCL_DEFAULT_PROTOCOL);
    EXPECT_EQ(config.mtcl_port, CAPIO_MTCL_DEFAULT_PORT);
    EXPECT_EQ(config.mtcl_poll_interval_us, CAPIO_MTCL_DEFAULT_POLL_INTERVAL);
    EXPECT_EQ(config.cache_lines, CAPIO_CACHE_LINES_DEFAULT);
    EXPECT_EQ(config.cache_line_size, CAPIO_CACHE_LINE_SIZE_DEFAULT);
    EXPECT_EQ(config.file_initial_size, CAPIO_DEFAULT_FILE_INITIAL_SIZE);
    EXPECT_EQ(config.prefetch_data_size, 0);
    EXPECT_FALSE(config.continue_on_error);
    // CAPIO-CL keys are not forwarded unless set, so CAPIO-CL keeps its own defaults
    EXPECT_EQ(capio_cl(config, "capiocl.workflow_name"), "<unset>");
    EXPECT_EQ(capio_cl(config, "capiocl.config_path"), "<unset>");
}

TEST_F(RuntimeConfigurationTest, ParsesFile) {
    const auto config = load_config(write(R"(
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

    EXPECT_EQ(config.capio_dir, std::filesystem::canonical("/tmp"));
    EXPECT_TRUE(config.continue_on_error);
    EXPECT_EQ(config.file_initial_size, 4096);
    EXPECT_EQ(config.prefetch_data_size, 1024);
    EXPECT_EQ(config.cache_lines, 20);
    EXPECT_EQ(config.cache_line_size, 8192);
    EXPECT_EQ(config.discovery_interface, CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.mcast_addr, "239.1.2.3");
    EXPECT_EQ(config.mcast_port, 12345);
    EXPECT_EQ(config.token_directory, "/tokens");
    EXPECT_EQ(config.backend_name, "mtcl");
    EXPECT_EQ(config.mtcl_protocol, "UCX");
    EXPECT_EQ(config.mtcl_port, 7601);
    EXPECT_EQ(config.mtcl_poll_interval_us, 50);

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

    const auto config =
        load_config(path, {"capio.storage.file_initial_size=16384", "capio.backend.type=mtcl",
                           "capiocl.workflow_name=wf", "capiocl.dynamic_api.enabled=true"});

    EXPECT_EQ(config.cache_lines, 30);          // environment
    EXPECT_EQ(config.cache_line_size, 4096);    // file beats environment
    EXPECT_EQ(config.file_initial_size, 16384); // override beats file
    EXPECT_EQ(config.backend_name, "mtcl");
    EXPECT_EQ(capio_cl(config, "capiocl.workflow_name"), "wf");
    EXPECT_EQ(capio_cl(config, "capiocl.dynamic_api.enabled"), "true");
}

TEST_F(RuntimeConfigurationTest, RejectsInvalidConfigurations) {
    // Typos are reported instead of being silently ignored
    EXPECT_THROW(load_config(write("[capio.cache]\nline = 3\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capoi]\ndirectory = \"/tmp\"\n")), std::runtime_error);
    // Wrong types
    EXPECT_THROW(load_config(write("[capio.cache]\nlines = \"3\"\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capio]\ncontinue_on_error = 1\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capio]\ndirectory = [\"/tmp\"]\n")), std::runtime_error);
    // Out of range and unknown choices
    EXPECT_THROW(load_config(write("[capio.cache]\nlines = 0\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capio.backend]\ntype = \"tcp\"\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capio.discovery]\ntype = \"dns\"\n")), std::runtime_error);
    EXPECT_THROW(load_config(write("[capio.backend.mtcl]\nport = 70000\n")), std::runtime_error);
    // Missing directory and broken TOML
    EXPECT_THROW(load_config(write("[capio]\ndirectory = \"/does/not/exist\"\n")),
                 std::runtime_error);
    EXPECT_THROW(load_config(write("[capio\n")), std::runtime_error);
    EXPECT_THROW(load_config(path.string() + ".missing"), std::runtime_error);
    // Invalid overrides
    EXPECT_THROW(load_config(std::nullopt, {"capio.cache.lines"}), std::runtime_error);
    EXPECT_THROW(load_config(std::nullopt, {"capio.cache.lines=ten"}), std::runtime_error);
    EXPECT_THROW(load_config(std::nullopt, {"capio.unknown=1"}), std::runtime_error);
    EXPECT_THROW(load_config(std::nullopt, {"capio.continue_on_error=yes"}), std::runtime_error);
    // Invalid environment
    setenv("CAPIO_CACHE_LINES", "many", 1);
    EXPECT_THROW(load_config(std::nullopt), std::runtime_error);
}

TEST_F(RuntimeConfigurationTest, ForwardsUnknownCapioClKeys) {
    const auto config = load_config(write("[capiocl.future]\noption = 3\n"));
    EXPECT_EQ(capio_cl(config, "capiocl.future.option"), "3");
}

TEST_F(RuntimeConfigurationTest, TemplateDocumentsEveryOptionWithItsDefault) {
    std::ostringstream output;
    write_config_template(output);
    const auto text = output.str();

    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        EXPECT_LE(line.size(), 100) << line;
    }

    // The template as generated, with everything commented out, means "all defaults"
    write(text);
    const auto defaults = load_config(std::nullopt);
    const auto as_is    = load_config(path);
    EXPECT_EQ(as_is.backend_name, defaults.backend_name);
    EXPECT_EQ(as_is.cache_lines, defaults.cache_lines);

    // Uncommenting every option must still give a valid configuration equal to the defaults
    const std::regex commented_option(R"(^# ([a-z_]+ = .*)$)", std::regex::multiline);
    write(std::regex_replace(text, commented_option, "$1"));
    const auto uncommented = load_config(path);
    EXPECT_EQ(uncommented.capio_dir, defaults.capio_dir);
    EXPECT_EQ(uncommented.discovery_interface, defaults.discovery_interface);
    EXPECT_EQ(uncommented.mcast_addr, defaults.mcast_addr);
    EXPECT_EQ(uncommented.mcast_port, defaults.mcast_port);
    EXPECT_EQ(uncommented.backend_name, defaults.backend_name);
    EXPECT_EQ(uncommented.mtcl_port, defaults.mtcl_port);
    EXPECT_EQ(uncommented.file_initial_size, defaults.file_initial_size);
    EXPECT_EQ(capio_cl(uncommented, "capiocl.config_path"), "");
    EXPECT_EQ(capio_cl(uncommented, "capiocl.workflow_name"), CAPIO_DEFAULT_WORKFLOW_NAME);
}

TEST_F(RuntimeConfigurationTest, EffectiveConfigurationCanBeLoadedBack) {
    std::ostringstream output;
    write_effective_config(output, std::nullopt,
                           {"capio.backend.type=mtcl", "capiocl.config_path=a \"quoted\" path"});
    write(output.str());

    const auto config = load_config(path);
    EXPECT_EQ(config.backend_name, "mtcl");
    EXPECT_EQ(capio_cl(config, "capiocl.config_path"), "a \"quoted\" path");
}

#include <gtest/gtest.h>

#include "common/constants.hpp"
#include "utils/runtime_configuration.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

TEST(RuntimeConfigurationTest, ParsesCapioAndExtractsCapioClConfiguration) {
    const auto path = std::filesystem::temp_directory_path() / "capio-runtime-config-test.toml";
    {
        std::ofstream config(path);
        config << R"(
[capiocl]
config_path = "workflow.json"
resolve_path = "/prefix"
store_all_in_memory = true
[capiocl.monitor.mcast]
enabled = true
[capiocl.dynamic_api]
enabled = true

[capio]
directory = "/tmp"
continue_on_error = true
[capio.storage]
file_initial_size = 4096
prefetch_data_size = 1024
[capio.cache]
lines = 20
line_size = 8192
[capio.discovery_service]
type = "fs"
[capio.discovery_service.mcast]
addr = "239.1.2.3"
port = 12345
[capio.discovery_service.fs]
token_directory = "/tokens"
[capio.backend]
type = "mtcl"
[capio.backend.mtcl]
proto = "TCP"
port = 7601
poll_interval_us = 50
)";
    }

    const auto config = parse_config(path);
    std::filesystem::remove(path);

    std::string config_path, resolve_path, dynamic_enabled, store_all_in_memory,
        multicast_enabled;
    config.capio_cl_config.getParameter("config_path", &config_path, "");
    config.capio_cl_config.getParameter("resolve_path", &resolve_path, "");
    config.capio_cl_config.getParameter("dynamic_api.enabled", &dynamic_enabled, "false");
    config.capio_cl_config.getParameter("store_all_in_memory", &store_all_in_memory, "false");
    config.capio_cl_config.getParameter("monitor.mcast.enabled", &multicast_enabled, "false");
    EXPECT_EQ(config_path, "workflow.json");
    EXPECT_EQ(resolve_path, "/prefix");
    EXPECT_EQ(dynamic_enabled, "true");
    EXPECT_EQ(store_all_in_memory, "true");
    EXPECT_EQ(multicast_enabled, "true");
    EXPECT_EQ(config.discovery_interface, CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.mcast_addr, "239.1.2.3");
    EXPECT_EQ(config.mcast_port, 12345);
    EXPECT_EQ(config.token_directory, "/tokens");
    EXPECT_EQ(config.backend_name, "mtcl");
    EXPECT_EQ(config.backend_options, "TCP:7601@50");
    EXPECT_EQ(config.capio_dir, "/tmp");
    EXPECT_EQ(config.cache_lines, 20);
    EXPECT_EQ(config.cache_line_size, 8192);
    EXPECT_EQ(config.capio_file_default_init_size, 4096);
    EXPECT_EQ(config.capio_prefetch_data_size, 1024);
    EXPECT_TRUE(config.continue_on_error);
}

TEST(RuntimeConfigurationTest, GeneratesDocumentedDefaultConfiguration) {
    std::ostringstream output;
    write_default_config(output);

    EXPECT_NE(output.str().find(CAPIO_SERVER_ARG_PARSER_DISCOVERY_HELP), std::string::npos);
    EXPECT_NE(output.str().find("# \t> mtcl"), std::string::npos);
    std::istringstream lines(output.str());
    for (std::string line; std::getline(lines, line);) {
        EXPECT_LE(line.size(), 120) << line;
    }

    const auto path = std::filesystem::temp_directory_path() / "capio-default-config-test.toml";
    {
        std::ofstream config(path);
        config << output.str();
    }
    const auto config = parse_config(path);
    std::filesystem::remove(path);

    EXPECT_EQ(config.backend_name, "none");
    EXPECT_EQ(config.discovery_interface, CAPIO_MCAST_PROTO_FLAG);

    const auto defaults = default_config();
    EXPECT_EQ(defaults.backend_name, config.backend_name);
    EXPECT_EQ(defaults.discovery_interface, config.discovery_interface);
}

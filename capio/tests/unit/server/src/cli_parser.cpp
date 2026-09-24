#include <gtest/gtest.h>

#include "common/constants.hpp"
#include "utils/cli_parser.hpp"

namespace {

CapioParsedConfig parse(std::vector<std::string> arguments) {
    std::vector<char *> argv;
    argv.reserve(arguments.size());
    for (auto &argument : arguments) {
        argv.push_back(argument.data());
    }
    return parseCLI(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(CliParserTest, UsesDefaultsWithNoConfig) {
    const auto config = parse({"capio_server", "--no-config"});

    EXPECT_EQ(config.discovery_interface, CAPIO_MCAST_PROTO_FLAG);
    EXPECT_EQ(config.mcast_addr, CAPIO_MCAST_ADV_DEFAULT_ADDR);
    EXPECT_EQ(config.mcast_port, CAPIO_MCAST_ADV_DEFAULT_PORT);
    EXPECT_EQ(config.token_directory, ".capio_tokens/");
    EXPECT_TRUE(config.backend_name.empty());
    EXPECT_FALSE(config.store_all_in_memory);
}

TEST(CliParserTest, ParsesExplicitOptions) {
    const auto config = parse({"capio_server", "--config", "config.json", "--resolve-prefix",
                               "/prefix", "--backend", "mtcl", "--backend-options", "TCP:7601@50",
                               "--discovery", "fs", "--mcast-addr", "239.1.2.3", "--mcast-port",
                               "12345", "--token-directory", "/tokens", "--mem-only"});

    EXPECT_EQ(config.capio_cl_config_path, "config.json");
    EXPECT_EQ(config.capio_cl_resolve_path, "/prefix");
    EXPECT_EQ(config.backend_name, "mtcl");
    EXPECT_EQ(config.backend_options, "TCP:7601@50");
    EXPECT_EQ(config.discovery_interface, CAPIO_FS_PROTO_FLAG);
    EXPECT_EQ(config.mcast_addr, "239.1.2.3");
    EXPECT_EQ(config.mcast_port, 12345);
    EXPECT_EQ(config.token_directory, "/tokens");
    EXPECT_TRUE(config.store_all_in_memory);
}

TEST(CliParserTest, RecognizesDynamicConfig) {
    const auto config = parse({"capio_server", "--config", "dynamic"});

    EXPECT_TRUE(config.capio_cl_dynamic_config);
}

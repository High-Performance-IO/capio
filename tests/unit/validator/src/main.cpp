#include <gtest/gtest.h>

#include <string>

std::string workflow_name;

#include <json_parser.hpp>

TEST(test_capio_cl_validator, test_json_parser_good_file) {

    char path[PATH_MAX];
    sprintf(path, "%s/test1.json", CAPIO_INSTALL_TARGET_DIR);
    std::cout << "Testing with file: " << path << std::endl;

    EXPECT_TRUE(JsonParser::parse(path, "./") != nullptr);
}

TEST(test_capio_cl_validator, test_json_parser_errors) {

    char path[PATH_MAX];

    sprintf(path, "%s/test2.json", CAPIO_INSTALL_TARGET_DIR);
    std::cout << "Testing with file: " << path << std::endl;
    EXPECT_TRUE(JsonParser::parse(path, "./") == nullptr);

    sprintf(path, "%s/test3.json", CAPIO_INSTALL_TARGET_DIR);
    std::cout << "Testing with file: " << path << std::endl;
    EXPECT_TRUE(JsonParser::parse(path, "./") == nullptr);

    EXPECT_TRUE(JsonParser::parse("", "./") == nullptr);
    EXPECT_TRUE(JsonParser::parse("/tmp/test_file_not_exists", "./") == nullptr);
}

int main(int argc, char *argv[]) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
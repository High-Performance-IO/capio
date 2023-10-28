#include <catch2/catch_test_macros.hpp>
#include "../../src/server/utils/json.hpp"
#include "../../src/server/utils/types.hpp"



TEST_CASE("Test json parsing against known and expected output", "[server]") {

    const std::string json_src_1("./json_test_src/json_src_1.json");
    const std::string json_src_2("./json_test_src/json_src_2.json");
    const std::string json_src_3("./json_test_src/json_src_3.json");
    const std::string capio_dir = ".";

    parse_conf_file(json_src_1, &capio_dir);




    parse_conf_file(json_src_2, &capio_dir);


    parse_conf_file(json_src_3, &capio_dir);
}



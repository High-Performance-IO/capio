#include <catch2/catch_test_macros.hpp>

#include <iostream>

#include "utils/capio_file.hpp"

TEST_CASE("Test inserting a single sector", "[server]") {
    Capio_file c_file;
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST_CASE("Test inserting two non-overlapping sectors", "[server]") {
    Capio_file c_file;
    c_file.insert_sector(5, 7);
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST_CASE("Test inserting two overlapping sectors starting at the same offset", "[server]") {
    Capio_file c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST_CASE("Test inserting two overlapping sectors ending at the same offset", "[server]") {
    Capio_file c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 4);
    c_file.print(std::cout);
}

TEST_CASE("Test inserting two overlapping sectors with one nested in the other", "[server]") {
    Capio_file c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 3);
    c_file.print(std::cout);
}
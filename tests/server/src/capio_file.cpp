#include <gtest/gtest.h>

#include <iostream>

#include "capio/env.hpp"
#include "utils/capio_file.hpp"

TEST(ServerTest, TestInsertSingleSector) {
    CapioFile c_file;
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST(ServerTest, TestInsertTwoNonOverlappingSectors) {
    CapioFile c_file;
    c_file.insert_sector(5, 7);
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST(ServerTest, TestInsertTwoOverlappingSectors) {
    CapioFile c_file;
    c_file.insert_sector(2, 4);
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameStart) {
    CapioFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(1, 3);
    c_file.print(std::cout);
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameEnd) {
    CapioFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 4);
    c_file.print(std::cout);
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsNested) {
    CapioFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 3);
    c_file.print(std::cout);
}
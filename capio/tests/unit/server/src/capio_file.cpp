#ifndef CAPIO_CAPIO_FILE_HPP
#define CAPIO_CAPIO_FILE_HPP
#include "server/include/storage/capio_file.hpp"
#include "common/env.hpp"
#include <gtest/gtest.h>

TEST(ServerTest, TestInsertSingleSector) {
    CapioFile c_file;
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 3L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoNonOverlappingSectors) {
    CapioFile c_file;
    c_file.insertSector(5, 7);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 2);
    auto it = sectors.begin();
    EXPECT_EQ(std::make_pair(1L, 3L), *it);
    std::advance(it, 1);
    EXPECT_EQ(std::make_pair(5L, 7L), *it);
}

TEST(ServerTest, TestInsertTwoOverlappingSectors) {
    CapioFile c_file;
    c_file.insertSector(2, 4);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameStart) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(1, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameEnd) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(2, 4);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsNested) {
    CapioFile c_file;
    c_file.insertSector(1, 4);
    c_file.insertSector(2, 3);
    auto &sectors = c_file.getSectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}
#endif // CAPIO_CAPIO_FILE_HPP

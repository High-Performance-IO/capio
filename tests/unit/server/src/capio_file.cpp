#include <gtest/gtest.h>

#include <iostream>

#include "capio/env.hpp"
#include "utils/capio_file/capio_file.hpp"

TEST(ServerTest, TestInsertSingleSector) {
    CapioMemFile c_file("test");
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 3L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoNonOverlappingSectors) {
    CapioMemFile c_file("test");
    c_file.insert_sector(5, 7);
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 2);
    auto it = sectors.begin();
    EXPECT_EQ(std::make_pair(1L, 3L), *it);
    std::advance(it, 1);
    EXPECT_EQ(std::make_pair(5L, 7L), *it);
}

TEST(ServerTest, TestInsertTwoOverlappingSectors) {
    CapioMemFile c_file("test");
    c_file.insert_sector(2, 4);
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameStart) {
    CapioMemFile c_file("test");
    c_file.insert_sector(1, 4);
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameEnd) {
    CapioMemFile c_file("test");
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 4);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsNested) {
    CapioMemFile c_file("test");
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}
#include <gtest/gtest.h>

#include <iostream>

#include "capio/env.hpp"
#include "utils/capio_file.hpp"

class CapioTestFile : public CapioFile {
  public:
    void insert_sector(off64_t new_start, off64_t new_end) { _insert_sector(new_start, new_end); }
};

TEST(ServerTest, TestInsertSingleSector) {
    CapioTestFile c_file;
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 3L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoNonOverlappingSectors) {
    CapioTestFile c_file;
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
    CapioTestFile c_file;
    c_file.insert_sector(2, 4);
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameStart) {
    CapioTestFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(1, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsSameEnd) {
    CapioTestFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 4);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}

TEST(ServerTest, TestInsertTwoOverlappingSectorsNested) {
    CapioTestFile c_file;
    c_file.insert_sector(1, 4);
    c_file.insert_sector(2, 3);
    auto &sectors = c_file.get_sectors();
    EXPECT_EQ(sectors.size(), 1);
    EXPECT_NE(sectors.find({1L, 4L}), sectors.end());
}
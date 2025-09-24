#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

const std::string filename = "hello.txt";
constexpr size_t textSize  = 32 * 1024 * 1024; // 32 MBB

inline std::string generateLongText() {
    std::string pattern = "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n";
    std::string longText;
    while (longText.size() + pattern.size() <= textSize) {
        longText += pattern;
    }
    // Pad the remaining bytes
    if (longText.size() < textSize) {
        longText.append(textSize - longText.size(), 'X');
    }
    return longText;
}

TEST(CapioMemoryFileTest, TestReadAndWrite32MBFile) {
    std::string longText = generateLongText();

    std::ofstream outFile(filename, std::ios::out | std::ios::binary);
    EXPECT_TRUE(outFile.is_open());
    outFile.write(longText.c_str(), longText.size());
    outFile.close();

    std::string fileContent(textSize, ' ');
    std::ifstream inFile(filename, std::ios::in | std::ios::binary);

    EXPECT_TRUE(inFile.is_open());
    EXPECT_TRUE(inFile.read(fileContent.data(), fileContent.size()));

    inFile.close();

    EXPECT_EQ(fileContent.size(), longText.size());

    for (size_t i = 0; i < longText.size(); ++i) {
        EXPECT_EQ(fileContent[i], longText[i]);
    }
}

int main(int argc, char **argv, char **envp) {
    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
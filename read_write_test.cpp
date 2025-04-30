#include <iostream>
#include <fstream>
#include <string>
#include <vector>

const std::string filename = "hello.txt";
const size_t textSize = 16 * 1024 * 1024; // 32 MBB

// Generate a large text (e.g., repeating a pattern)
std::string generateLongText() {
    std::string pattern = "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n";
    std::string longText;
    while (longText.size() + pattern.size() <= textSize) {
        longText += pattern;
    }
    // If needed, pad the remaining bytes
    if (longText.size() < textSize) {
        longText.append(textSize - longText.size(), 'X');
    }
    return longText;
}

void writeToFile(const std::string& content) {
    std::ofstream outFile(filename, std::ios::out | std::ios::binary);
    if (!outFile) {
        std::cout << "Error opening file for writing!\n";
        return;
    }
    outFile.write(content.c_str(), content.size());
    outFile.close();
    std::cout << "Successfully wrote " << content.size() << " bytes to " << filename << "\n";
}

void readFromFile(const std::string& expectedContent) {
    std::ifstream inFile(filename, std::ios::in | std::ios::binary);
    if (!inFile) {
        std::cout << "Error opening file for reading!\n";
        return;
    }

    std::string fileContent((std::istreambuf_iterator<char>(inFile)),
                              std::istreambuf_iterator<char>());
    inFile.close();

    bool match = true;
    size_t minLength = std::min(fileContent.size(), expectedContent.size());

    for (size_t i = 0; i < minLength; ++i) {
        if (fileContent[i] != expectedContent[i]) {
            std::cout << "Mismatch at position " << i << ":\n";
            std::cout << "Expected: '" << expectedContent[i] << "' (ASCII " << static_cast<int>(expectedContent[i]) << ")\n";
            std::cout << "Found:    '" << fileContent[i] << "' (ASCII " << static_cast<int>(fileContent[i]) << ")\n";
            match = false;
            exit(-1);
        }
    }

    if (match) {
        if (fileContent.size() != expectedContent.size()) {
            std::cout << "Mismatch in size!\n";
            std::cout << "Expected size: " << expectedContent.size() << ", Found size: " << fileContent.size() << "\n";
            match = false;
        } else {
            std::cout << "Content matches expected text.\n";
        }
    }
  //  std::cout << "\nFile content:\n" << fileContent << "\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " --write | --read\n";
        return 1;
    }

    std::string longText = generateLongText();
    std::string option = argv[1];

    if (option == "--write") {
        writeToFile(longText);
    } else if (option == "--read") {
        readFromFile(longText);
    } else {
        std::cout << "Invalid option. Use --write or --read.\n";
        return 1;
    }

    return 0;
}

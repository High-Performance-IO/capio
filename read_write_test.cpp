#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

const std::string filename = "hello.txt";
const size_t textSize = 32 * 1024 * 1024; // 32 MBB

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


void print_surrounding_lines(const std::string &content, std::size_t offset) {
    if (offset >= content.size()) {
        std::cerr << "Offset is out of range.\n";
        return;
    }

    // Find start of current line
    std::size_t line_start = content.rfind('\n', offset);
    line_start = (line_start == std::string::npos) ? 0 : line_start + 1;

    // Find end of current line
    std::size_t line_end = content.find('\n', offset);
    line_end = (line_end == std::string::npos) ? content.size() : line_end;

    // Extract current line
    std::string current_line = content.substr(line_start, line_end - line_start);

    // Find previous line
    if (line_start > 0) {
        const std::size_t prev_end = line_start - 1;
        std::size_t prev_start = content.rfind('\n', prev_end);
        prev_start = (prev_start == std::string::npos) ? 0 : prev_start + 1;
        const std::string prev_line = content.substr(prev_start, prev_end - prev_start + 1);
        if (!prev_line.empty()) {
            std::cout << "Previous line : [" << prev_start << "]" << prev_line << "\n";
        }
    } else {
        std::cout << "Previous line: [None]\n";
    }

    std::cout << "Offending line: [" << line_start << "] " << current_line << "\n";

    // Find next line
    if (line_end < content.size()) {
        std::size_t next_start = line_end + 1;
        std::size_t next_end = content.find('\n', next_start);
        if (next_end == std::string::npos)
            next_end = content.size();
        std::string next_line = content.substr(next_start, next_end - next_start);

        if (!next_line.empty()) {
            std::cout << "Next line     : [" << next_start << "]" << next_line << "\n";
        }
    } else {
        std::cout << "Next line:     [None]\n";
    }

}


void readFromFile(const std::string &expectedContent) {

    std::string fileContent(textSize, ' ');

    auto write_start = std::chrono::high_resolution_clock::now();

    std::ifstream inFile(filename, std::ios::in | std::ios::binary);
    if (!inFile) {
        std::cout << "Error opening file for reading!\n";
        return;
    }
    if (!inFile.read(fileContent.data(), fileContent.size())) {
        std::cerr << "Failed to read file content.\n";
        exit(-1);
    }
    inFile.close();

    auto write_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = write_end - write_start;
    double bandwidthMBps = (textSize / (1024.0 * 1024.0)) / elapsed.count();
    std::cout << "READ(): " << bandwidthMBps << " MB/s (in " << elapsed.count() << ")" << std::endl;

    bool match = true;
    size_t minLength = std::min(fileContent.size(), expectedContent.size());

    if (fileContent.size() != expectedContent.size()) {
        std::cout << "Mismatch in size!\n";
        std::cout << "Expected size: " << expectedContent.size() << ", Found size: " <<
            fileContent.size() << "\n";
        exit(-1);
    }

    for (size_t i = 0; i < minLength; ++i) {
        if (fileContent[i] != expectedContent[i]) {
            std::cout << "Mismatch at position " << i << ":\n";
            std::cout << "Expected: '" << expectedContent[i] << "' (ASCII " << static_cast<int>(
                expectedContent[i]) << ")\n";
            std::cout << "Found:    '" << fileContent[i] << "' (ASCII " << static_cast<int>(
                fileContent[i]) << ")\n";
            print_surrounding_lines(fileContent, i);
            exit(-1);
        }
    }
    std::cout << "Content matches expected text.\n";

}

int main(int argc, char *argv[]) {
    std::string longText = generateLongText();
    auto write_start = std::chrono::high_resolution_clock::now();
    std::ofstream outFile(filename, std::ios::out | std::ios::binary);
    if (!outFile) {
        std::cout << "Error opening file for writing!\n";
        exit(-1);
    }
    outFile.write(longText.c_str(), longText.size());
    outFile.close();
    std::cout << "Successfully wrote " << longText.size() << " bytes to " << filename << std::endl;

    /*Read a single byte to wait for caches to empty*/
    std::ifstream file(filename, std::ios::binary);
    char byte;
    file.read(&byte, 1); // Read a single byte
    file.close();
    auto write_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = write_end - write_start;
    double bandwidthMBps = (textSize / (1024.0 * 1024.0)) / elapsed.count();

    std::cout << "WRITE(): " << bandwidthMBps << " MB/s" << std::endl;

    readFromFile(longText);

    return 0;
}
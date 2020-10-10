#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    std::ifstream file("output_file.txt");
    if (argc != 3) {
        std::cout << "input error: num_rows and num_cols needed" << std::endl;
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    if (file.is_open()) {
        int* data = new int[num_rows * num_cols];
        for (int i = 0; i < num_rows * num_cols; ++i) {
            file >> data[i];
        }
        for (int i = 0; i < num_rows * num_cols; ++i) {
            std::cout << data[i] << "\n";
        }
        free(data);
    }
    else {
        std::cout << "impossible to open the file" << std::endl;
    }
    file.close();
}
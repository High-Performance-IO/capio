#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "input error: num_rows and num_cols needed" << std::endl;
        return 1;
    }
    int num_rows = std::stoi(argv[1]);
    int num_cols = std::stoi(argv[2]);
    std::ofstream file("output_file.txt");
    int* data = new int[num_rows * num_cols];
    for (int i = 0; i < num_rows * num_cols; ++i)
        data[i] = i;
    for (int i = 0; i < num_rows * num_cols; ++i)
        file << data[i] << " ";
    free(data);
    file.close();
    return 0;
}
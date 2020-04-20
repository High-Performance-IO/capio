#include <iostream>
#include "../capio.hpp"
int main(int argc, char** argv) {
    capio_proxy<int> proxy("outputfile5","producer",10);
    std::cout << "after constuctor\n";
    for (int i = 0; i < 45; ++i) {
        proxy.write(i);
        std::cout << "write\n";
    }
    std::cout << "before finished\n";
    proxy.finished();

}


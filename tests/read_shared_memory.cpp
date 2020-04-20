#include <iostream>
#include <cassert>
#include "../capio.hpp"

/*
 * single consumer that read a sequence of integers through the capio proxy
 * to use in combination with a producer
 */

int main(int argc, char** argv) {
    capio_proxy<int> proxy("outputfile5", 10);
    std::cout << "after constuctor\n";
    int j = 0;
    while (! proxy.done()) {
        int i = proxy.read();
        std::cout << "read\n";
        assert(i == j);
        std::cout << i << "\n";
        ++j;
    }
    std::cout << "before finished\n";
    proxy.clean_resources();
}



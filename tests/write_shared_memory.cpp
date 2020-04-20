#include <iostream>
#include "../capio.hpp"

/*
 * single consumer that write a sequence of integer of fixed length using the capio proxy
 * to use in combination with a consumer
 */

int main(int argc, char** argv) {
    capio_proxy<int> proxy("outputfile5", 10);
    std::cout << "after constuctor\n";
    for (int i = 0; i < 45; ++i) {
        proxy.write(i);
        std::cout << "write\n";
    }
    std::cout << "before finished\n";
    proxy.finished();

}


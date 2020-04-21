#include <iostream>
#include "../capio.hpp"

struct elem {
    int i;
    double d;
    elem() {
        i = 0;
        d = 0;
    }
};

/*
 * single producer that write a sequence of integers and a sequence of structs using the capio proxy
 * to use in combination with a consumer
 */

int const num_writes = 45;

int main(int argc, char** argv) {
    capio_proxy<int> proxy("outputfile7", 1, 10);
    std::cout << "after constuctor\n";
    for (int i = 0; i < num_writes; ++i) {
        proxy.write(i);
        std::cout << "write\n";
    }
    std::cout << "before finished\n";
    proxy.finished();
    capio_proxy<struct elem> proxy2("outputfile8", 1, 12);
    std::cout << "after constuctor\n";
    struct elem e;
    for (int i = 0; i < num_writes; ++i) {
        e.i = i;
        e.d = (double) i / num_writes;
        proxy2.write(e);
        std::cout << "write\n";
    }
    proxy2.finished();
}


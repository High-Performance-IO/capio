#include <iostream>
#include <assert.h>

void print_array(int data[], int array_length, int rank) {
    std::cout << "array_length: " << array_length << std::endl;
    for (int i = 0; i < array_length; ++i) {
        std::cout << "reader " << rank << " data: " << data[i] << std::endl;
    }
}

void compare_expected_actual(int* actual, int* expected, int array_length) {
    for (int i = 0; i < array_length; ++i) {
        assert(actual[i] == expected[i]);
    }
}


#ifndef UTILS_H
#define UTILS_H
#include <capio/constants.hpp>
#include <iostream>
#include <unistd.h>

template <typename T> void capio_delete(T **ptr) {
    if (*ptr != nullptr) {
        delete *ptr;
        *ptr = nullptr;
    }
#ifndef __CAPIO_POSIX
    else {
        char nodename[HOST_NAME_MAX]{0};
        gethostname(nodename, HOST_NAME_MAX);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << nodename << " ] "
                  << "Double delete detected! avoided segfault..." << std::endl;
    }
#endif
}


template <typename T> void capio_delete_vec(T **ptr) {
    if (*ptr != nullptr) {
        delete[] *ptr;
        *ptr = nullptr;
    }
#ifndef __CAPIO_POSIX
    else {
        char nodename[HOST_NAME_MAX]{0};
        gethostname(nodename, HOST_NAME_MAX);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_WARNING << " [ " << nodename << " ] "
                  << "Double delete[] detected! avoided segfault..." << std::endl;
    }
#endif
}

#endif // UTILS_H

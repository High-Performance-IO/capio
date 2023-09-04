#ifndef CAPIO_DATA_STRUCT_INCLUDE_HPP
#define CAPIO_DATA_STRUCT_INCLUDE_HPP

/**
 * Include this file to access all capio custom defined data structures
 */

#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <string>
#include <atomic>

#include <unistd.h>
#include <semaphore.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "capio_file.hpp"
#include "circular_buffer.hpp"
#include "data_structure.hpp"
#include "spsc_queue.hpp"

#endif //CAPIO_INCLUDE_HPP

#ifndef CAPIO_POSIX_UTILS_APP_HPP
#define CAPIO_POSIX_UTILS_APP_HPP

#include <cstdlib>

const char* get_capio_app_name() {
  static char* capio_app_name = nullptr;
  if (capio_app_name == nullptr) {
    capio_app_name = std::getenv("CAPIO_APP_NAME");
  }
  return capio_app_name;
}

int get_num_writes_batch() {
    static int num_writes_batch = 0;
    if (num_writes_batch == 0) {
        const char *val = std::getenv("CAPIO_GW_BATCH");
        if (val == nullptr) {
            num_writes_batch = std::stoi(val);
            if (num_writes_batch <= 0) {
                std::cerr << "error: CAPIO_GW_BATCH variable must be >= 0";
                exit(1);
            }
        } else {
            num_writes_batch = 1;
        }
    }
    return num_writes_batch;
}

#endif // CAPIO_POSIX_UTILS_APP_HPP

#ifndef CAPIO_PRINT_TEXT_HPP
#define CAPIO_PRINT_TEXT_HPP
#include <chrono>
#include <iostream>
#include <string_view>

#include "common/constants.hpp"

inline void server_println(const std::string_view workflow_name  = CAPIO_DEFAULT_WORKFLOW_NAME,
                           const std::string_view message_color  = CAPIO_LOG_SERVER_CLI_LEVEL_RESET,
                           const std::string_view component_name = "CAPIO",
                           const std::string_view message_line   = "") {

    static char node_name[HOST_NAME_MAX];
    // static init once the nodename
    [[maybe_unused]] static bool host_init = []() {
        if (gethostname(node_name, HOST_NAME_MAX) != 0) {
            snprintf(node_name, HOST_NAME_MAX, "unknown");
        }
        return true;
    }();

    if (message_color.empty()) {
        std::cout << message_line << "\n";
        return;
    }

    // Get current time
    const auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::cout << message_color << "[CAPIO-SERVER > " << workflow_name << "]";
    std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_RESET << " [";
    std::cout << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] ";
    std::cout << " " << node_name << "@" << std::left << std::setw(20);
    std::cout << component_name.substr(0, 20) << " | " << message_line << "\n";
    std::cout << std::flush;
}
#endif // CAPIO_PRINT_TEXT_HPP

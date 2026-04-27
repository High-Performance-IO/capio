#ifndef CAPIO_PRINT_TEXT_HPP
#define CAPIO_PRINT_TEXT_HPP
#include <chrono>
#include <iostream>
#include <string_view>

#include "common/constants.hpp"
#include "common/env.hpp"

inline void server_println(const std::string_view message_color  = "",
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

    std::cout << message_color << "[CAPIO-SERVER ~ " << get_capio_workflow_name() << "]"
              << CAPIO_LOG_SERVER_CLI_LEVEL_RESET << " ["
              << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "] " << " "
              << node_name << "@" << std::left << std::setw(20) << component_name.substr(0, 20)
              << " | " << message_line << "\n"
              << std::flush;
}
#endif // CAPIO_PRINT_TEXT_HPP

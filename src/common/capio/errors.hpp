#ifndef CAPIO_COMMON_ERRORS_HPP
#define CAPIO_COMMON_ERRORS_HPP

#include <cerrno>
#include <cstring>
#include <iostream>

static inline void err_exit(const std::string& error_msg,std::ostream& outstream = std::cerr) {
  outstream << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << std::strerror(errno) << std::endl;
  exit(1);
}

static inline void err_exit(const std::string& error_msg, const std::string& invoker, std::ostream& outstream = std::cerr) {
  outstream << "error: at " <<invoker <<", " << error_msg << " errno " <<  errno << " strerror(errno): " << std::strerror(errno) << std::endl;
  exit(1);
}

#endif // CAPIO_COMMON_ERRORS_HPP

#ifndef CAPIO_POSIX_UTILS_LOGGER_HPP
#define CAPIO_POSIX_UTILS_LOGGER_HPP

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

static inline void print_prefix(const char* str, const char* prefix, ...) {
  va_list argp, argpc;
  char format[256];
  strcpy(format,prefix);
  strcpy(format+strlen(prefix), str);
  va_start(argp, prefix);
  va_copy(argpc, argp);
  int size = vsnprintf(nullptr, 0U, format, argp);
  std::unique_ptr<char[]> buf(new char[size]);
  vsnprintf(buf.get(), size + 1, format, argpc);
  syscall_no_intercept(SYS_write, fileno(stderr), buf.get(), size);
  va_end(argp);
  va_end(argpc);
  fflush(stderr);
}

#ifdef CAPIOLOG
#define CAPIO_DBG(str, ...) \
  print_prefix(str, "DBG:", ##__VA_ARGS__)
#else
#define CAPIO_DBG(str, ...)
#endif

#endif // CAPIO_POSIX_UTILS_LOGGER_HPP

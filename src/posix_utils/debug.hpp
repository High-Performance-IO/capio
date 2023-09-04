#ifndef CAPIO_DEBUG_HPP
#define CAPIO_DEBUG_HPP

#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR   512
#endif

#ifdef CAPIOLOG
#define CAPIO_DBG(str, ...) \
  print_prefix(str, "DBG:", ##__VA_ARGS__)

  static inline void print_prefix(const char* str, const char* prefix, ...) {
    va_list argp;
    char p[256];
    strcpy(p,prefix);
    strcpy(p+strlen(prefix), str);
    va_start(argp, prefix);
    vfprintf(stderr, p, argp);
    va_end(argp);
    fflush(stderr);
}
#else
#define CAPIO_DBG(str, ...)
#endif


#endif //CAPIO_DEBUG_HPP

#ifndef CAPIO_POSIX_UTILS_STORAGE_HPP
#define CAPIO_POSIX_UTILS_STORAGE_HPP

// Macro to select at compile time either FS or MEM streaming
// CAPIO_STORAGE_CALL(memory_call, fs_call)
#if CAPIO_MEM_STREAM
#define CAPIO_STORAGE_CALL(memory_call, fs_call) memory_call
#else
#define CAPIO_STORAGE_CALL(memory_call, fs_call) fs_call
#endif

#endif // CAPIO_POSIX_UTILS_STORAGE_HPP

# Posix systemcall handlers for CAPIO

This folder contains syscall handlers for capio.

## Launch

The interception backend is selected at build time by the CPU architecture
(see `CMakeLists.txt`): riscv uses vpoline, everything else uses syscall_intercept.
Both are injected at runtime, never linked:

```
# x86_64 / aarch64 (syscall_intercept hooks the process directly)
LD_PRELOAD=./path/to/libcapio_posix.so ./my_app

# riscv (vpoline patches ECALLs, then dlmopens the CAPIO hook)
LD_PRELOAD=./path/to/libvpoline.so LIBVPHOOK=./path/to/libcapio_posix.so ./my_app
```

## Implement a new systemcall

For each new systemcall implemented in CAPIO, unless the
behaviour is the same of a previously added systemcall, a new file should be added. The handler must have
the following signature:

```
int(*)(long, long, long, long, long, long, long*,long)
```

## Register a new capio posix handler

To be able to capture the newly implemented syscall, two things have to be done:

* Add the newly created file as an include into `src/posix/handlers.hpp`
* In the file `src/posix/capio_posix.cpp`, in the function `build_syscall_table()`, add a new entry to
  the `_syscallTable`
* array. The index of the array should be the syscall that is being implemented, and the value should be the pointer to
  the function
# Posix systemcall handlers for CAPIO

This folder contains syscall handlers for capio.

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
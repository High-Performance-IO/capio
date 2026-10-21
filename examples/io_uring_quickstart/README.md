# io_uring quickstart

This example shows how to run an io_uring application under CAPIO. The
application does not change: you only start it with the CAPIO library.

There are two workflow steps:

- `producer` writes five messages to `messages.txt` with io_uring.
- `consumer` reads `messages.txt` with io_uring until end of file.

With CAPIO, the consumer starts first and reads each message while the
producer writes it. The file stays in CAPIO memory and does not go to the disk.

## Requirements

- CAPIO, built or installed (see the main README).
- `liburing` development files (for example, `liburing-dev`).
- `cmake` and a C++17 compiler.

## Run

```
./run.sh                                  # with an installed CAPIO
CAPIO_BUILD=/path/to/capio/build ./run.sh # with a CAPIO build directory
```

Expected output (the order of the lines can change a little):

```
consumer: read    message 1 of 5
producer: wrote   message 1 of 5
producer: wrote   message 2 of 5
consumer: read    message 2 of 5
...
producer: closed the file
consumer: end of file after 20480 bytes

Done: the consumer read the 5 messages through CAPIO while the producer wrote them.
messages.txt is not in /tmp/capio_quickstart.XXXX: CAPIO kept it in memory.
```

## What run.sh does

1. It builds `producer` and `consumer` (`CMakeLists.txt`).
2. It starts the CAPIO server with the workflow configuration:

   ```
   CAPIO_DIR=$dir capio_server -c capio_config.json -r $dir
   ```

   `-r` resolves the relative paths in `capio_config.json` against `CAPIO_DIR`.
   Without `-r`, the rule does not match `messages.txt`.

3. It starts each step with the CAPIO library:

   ```
   CAPIO_DIR=$dir CAPIO_WORKFLOW_NAME=io_uring_quickstart CAPIO_APP_NAME=consumer \
     INTERCEPT_ALL_OBJS=1 LD_PRELOAD=libcapio_posix.so ./build/consumer $dir/messages.txt
   ```

   The producer uses the same command with `CAPIO_APP_NAME=producer`.

## Settings

| Setting | Why |
|---|---|
| `INTERCEPT_ALL_OBJS=1` | liburing makes the io_uring syscalls itself, not through libc. This setting lets CAPIO intercept them. Without it, the first write fails with `EBADF`. |
| `CAPIO_APP_NAME` | Connects each process to its step in `capio_config.json`. |
| `CAPIO_CACHE_LINE_SIZE=4096` | CAPIO sends written data to the server one cache line at a time (default 256 KiB). A 4 KiB line sends each 4 KiB message at once. Use the same value for the server and the applications. |

The configuration in `capio_config.json` makes `messages.txt` a stream:
`"committed": "on_close"` gives end of file to the consumer only when the
producer closes the file, and `"mode": "no_update"` lets the consumer read
data before that.

## Without CAPIO

The two programs are normal io_uring programs. Without CAPIO, run the
producer first, because the consumer stops at the current end of the file:

```
./build/producer /tmp/messages.txt
./build/consumer /tmp/messages.txt
```

## Limits

On CAPIO files, CAPIO supports these io_uring operations: `READ`, `READV`,
`WRITE`, `WRITEV`, `READ_FIXED`, `WRITE_FIXED`, `FSYNC` and `CLOSE`; and on CAPIO
paths: `OPENAT`, `OPENAT2`, `STATX`, `UNLINKAT`, `MKDIRAT` and `RENAMEAT`. CAPIO
runs these operations synchronously, inside `io_uring_enter`.

Every other operation, and every operation on other files, sockets or pipes, goes
to a kernel io_uring ring that CAPIO keeps for each ring. It works as without
CAPIO.

A linked chain can use CAPIO files or other files, not both: in a chain that
mixes them, the operations on other files fail with `EINVAL` (a `NOP` is fine). CAPIO files cannot
be registered (`io_uring_register_files`). `SQPOLL`, `IOPOLL` and the other setup
modes that change the ring layout are not supported.

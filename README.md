# CAPIO

CAPIO (Cross-Application Programmable I/O), is a middleware aimed at injecting streaming capabilities to workflow steps
without changing the application codebase. It has been proven to work with C/C++ binaries, Fortran Binaries, JAVA,
python and bash. 

[![codecov](https://codecov.io/gh/High-Performance-IO/capio/graph/badge.svg?token=6ATRB5VJO3)](https://codecov.io/gh/High-Performance-IO/capio)
![CI-Tests](https://github.com/High-Performance-IO/capio/actions/workflows/ci-tests.yaml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://raw.githubusercontent.com/High-Performance-IO/capio/master/LICENSE)

## Capio Bibliography

- [![CAPIO methodology](https://img.shields.io/badge/CAPIO%20Methodology%20(Use%20this%20to%20cite%20CAPIO)-10.1016/j.future.2025.108159-%23cc5500?logo=doi&logoColor=white&labelColor=2b2b2b)](http://dx.doi.org/10.1016/j.future.2025.108159)
- [![CAPIO middleware](https://img.shields.io/badge/HiPC%202023%20Paper-10.1109/HiPC58850.2023.00031-%23cc5500?logo=doi&logoColor=white&labelColor=2b2b2b)](http://dx.doi.org/10.1109/HiPC58850.2023.00031)

## Build and run tests

### Dependencies

CAPIO depends on the following software that needs to be manually installed:

- `cmake >=3.15`
- `c++17` or newer
- `pthreads`

`openmpi` is optional. When CMake finds it, CAPIO builds and enables the `mpi` and `mpisync` server backends;
otherwise, CAPIO builds without MPI support and the MTCL backend remains available.
The following dependencies are automatically fetched during the CMake configuration phase and built as needed.

- [CAPIO-CL](https://github.com/High-Performance-IO/CAPIO-CL) handles CAPIO-CL configuration and enforces streaming directives.
- [CALF](https://github.com/High-Performance-IO/CALF) provides logging and CLI output.
- [alpha-unito/syscall_intercept](https://github.com/alpha-unito/syscall_intercept) intercepts system calls (forked from `pmem/syscall_intercept`).
- [ParaGroup/MTCL](https://github.com/ParaGroup/MTCL) provides dynamic, multi-backend communication between CAPIO server instances.

### Compile capio

```bash
git clone https://github.com/High-Performance-IO/capio.git capio && cd capio
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

It is also possible to enable log in CAPIO, by defining `-DCAPIO_LOG=TRUE`.

## Use CAPIO in your code

Good news! You don't need to modify your code to benefit from the features of CAPIO. You have only to do three steps (
the first is optional).

1. Generate and edit the TOML configuration with `capio_server --genconf`, or use built-in defaults with `capio_server --defconf`.

2. Launch the CAPIO daemons with MPI, passing the TOML configuration file on the machines in which you
   want to execute your program (one daemon for each node). Set `capio.directory` in that file to
   choose the managed root directory.
   ```bash
   [mpiexec -N 1 --hostfile your_hostfile] capio_server default.toml
   ```

3. Launch your programs preloading the CAPIO shared library like this:
   ```bash
   CAPIO_DIR=your_capiodir      \
   CAPIO_WORKFLOW_NAME=wfname   \
   CAPIO_APP_NAME=appname       \
   LD_PRELOAD=libcapio_posix.so \
   ./your_app <args>
    ```

> [!WARNING]
> `CAPIO_DIR` must be specified when launching a program with the CAPIO library. if `CAPIO_DIR` is not specified, CAPIO
> will not intercept syscalls. Its value must match `capio.directory` in the server TOML configuration.

### Server backend options

Configure the backend in TOML. For example:

```toml
[capio.backend]
type = "mtcl"

[capio.backend.mtcl]
proto = "TCP"
listen_address = "0.0.0.0"
port = 7600
poll_interval_us = 1000000
```

If omitted, CAPIO uses the `none` backend. See the
[MTCL repository](https://github.com/ParaGroup/MTCL) for supported communication protocols and their requirements.

### Server configuration

The server reads runtime settings only from TOML. `capio_server --genconf` writes a documented `default.toml`, while
`capio_server --defconf` starts directly with the same built-in defaults. Important server settings include:

```toml
[capio]
directory = "."

[capio.storage]
file_initial_size = 4294967296
prefetch_data_size = 0

[capio.cache]
lines = 10
line_size = 262144
```

### POSIX environment variables

Preloaded applications remain separate processes and use these environment variables to connect to the configured
server:

#### Logging

- `CAPIO_LOG_LEVEL` controls the application log level. This variable works only
  if `-DCAPIO_LOG=TRUE` was specified during cmake phase;
- `CAPIO_LOG_PREFIX` specifies the prefix of
  the logfile name to which capio will log to. The default value is `posix_thread_`, which means that capio will log by
  default to a set of files called `posix_thread_*.log`;
- `CAPIO_LOG_DIR` specifies the log directory. It defaults to `capio_logs`.

#### Runtime

> [!WARNING]  
> The following variables are mandatory. If not provided to a posix, application, CAPIO will not be able to correctly
> handle the application according to the workflow configuration.

- `CAPIO_DIR`: must match `capio.directory` in the server TOML configuration;
- `CAPIO_WORKFLOW_NAME`: must match `capiocl.workflow_name` in the server TOML configuration;
- `CAPIO_APP_NAME`: This environment variable defines the app name within a workflow for a given step;
- `CAPIO_CACHE_LINES`: must match `capio.cache.lines`; defaults to 10;
- `CAPIO_CACHE_LINE_SIZE`: must match `capio.cache.line_size`; defaults to 256 KiB.

## How to inject streaming capabilities into your workflow

With CAPIO is possible to run the applications of your workflow that communicates through files concurrently. CAPIO will
synchronize transparently the concurrent reads and writes on those files. If a file is never modified after it is closed
you can set the streaming semantics equals to "on_close" on the configuration file. In this way, all the reads done on
this file will hung until the writer closes the file, allowing the consumer application to read the file even if the
producer is still running.
Another supported file streaming semantics is "append" in which a read is satisfied when the producer writes the
requested data. This is the most aggressive (and efficient) form of streaming semantics (because the consumer can start
reading while the producer is writing the file). This semantic must be used only if the producer does not modify a piece
of data after it is written.
The streaming semantic on_termination tells CAPIO to not allowing streaming on that file. This is the default streaming
semantics if a semantics for a file is not specified.
The following is an example of a simple configuration:

```json
{
  "name": "my_workflow",
  "IO_Graph": [
    {
      "name": "writer",
      "output_stream": [
        "file0.dat",
        "file1.dat",
        "file2.dat"
      ],
      "streaming": [
        {
          "name": ["file0.dat"],
          "committed": "on_close"
        },
        {
          "name": ["file1.dat"],
          "committed": "on_close",
          "mode": "no_update"
        },
        {
          "name": ["file2.dat"],
          "committed": "on_termination"
        }
      ]
    },
    {
      "name": "reader",
      "input_stream": [
        "file0.dat",
        "file1.dat",
        "file2.dat"
      ]
    }
  ]
}
```

> [!NOTE]
> We are working on an extension of the possible streaming semantics and in a detailed
> documentation about the configuration file!

## Examples

The [examples](examples) folder contains some examples that shows how to use mpi_io with CAPIO.
There are also examples on how to write JSON configuration files for the semantics implemented by CAPIO:

- [on_close](https://github.com/High-Performance-IO/capio/wiki/Examples#on_close-semantic): A pipeline composed by a
  producer and a consumer with "on_close" semantics
- [no_update](https://github.com/High-Performance-IO/capio/wiki/Examples#noupdate-semantics): A pipeline composed by a
  producer and a consumer with "no_update" semantics
- [mix_semantics](https://github.com/High-Performance-IO/capio/wiki/Examples#mixed-semantics): A pipeline composed by a
  producer and a consumer with mix semantics

## Report bugs + get help

[Create a new issue](https://github.com/High-Performance-IO/capio/issues/new)

[Get help](https://github.com/High-Performance-IO/capio/wiki)

> [!TIP]
> A [wiki](https://github.com/High-Performance-IO/capio/wiki) is in development! You might want to check the wiki to get
> more in depth information about CAPIO!

## CAPIO Team

Made with :heart: by:

Marco Edoardo Santimaria <marcoedoardo.santimaria@unito.it> (Designer and maintainer) \
Iacopo Colonnelli <iacopo.colonnelli@unito.it> (Workflows expert and maintainer) \
Massimo Torquati <massimo.torquati@unipi.it> (Designer) \
Marco Aldinucci <marco.aldinucci@unito.it> (Designer) \
Alberto Riccardo Martinelli <albertoriccardo.martinelli@unito.it> (designer and maintainer)

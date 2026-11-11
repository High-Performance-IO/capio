# CAPIO: Cross-Application Programmable I/O

CAPIO is a middleware aimed at injecting streaming capabilities into workflow steps without changing the application
codebase. It has been proven to work with C/C++ binaries, Fortran, Java, Python, and Bash.

[![codecov](https://codecov.io/gh/High-Performance-IO/capio/graph/badge.svg?token=6ATRB5VJO3)](https://codecov.io/gh/High-Performance-IO/capio)
![CI-Tests](https://github.com/High-Performance-IO/capio/actions/workflows/ci-tests.yaml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://raw.githubusercontent.com/High-Performance-IO/capio/master/LICENSE)

> [!TIP]
> MPI is optional. CAPIO servers can communicate through MTCL, or run with no backend at all to only coordinate I/O
> operations between workflow steps on a single node.

Supported architectures:

- ![x86_64](https://img.shields.io/badge/x86__64_/_amd64-supported-50C878.svg)
- ![ARM64](https://img.shields.io/badge/ARM64_/_aarch64-supported-50C878.svg)
- ![RISC-V](https://img.shields.io/badge/RISC--V_(riscv64)-in_progress-orange.svg)

## Install with Spack

CAPIO is available through the High Performance IO Spack repository. See
[High-Performance-IO/hpio-spack](https://github.com/High-Performance-IO/hpio-spack) for instructions.

## Manual build and install

### Dependencies

Required: `cmake >= 3.15`, a `C++17` (or newer) compiler (CI tests GCC 12–16 and Clang 13–23), `pthreads`.

Optional: MPI. When CMake finds it, CAPIO builds the `mpi` and `mpisync` server backends. Otherwise CAPIO builds
without MPI support, and the `mtcl` backend remains available.

Fetched and built during the CMake configuration:

- [CAPIO-CL](https://github.com/High-Performance-IO/CAPIO-CL) parses CAPIO-CL configuration files and enforces
  streaming directives.
- [CALF](https://github.com/High-Performance-IO/calf) provides logging and CLI output.
- [alpha-unito/syscall_intercept](https://github.com/alpha-unito/syscall_intercept) intercepts system calls (a fork of
  `pmem/syscall_intercept` with aarch64 support).
- [ParaGroup/MTCL](https://github.com/ParaGroup/MTCL) provides multi-protocol communication between CAPIO servers.
- [Taywee/args](https://github.com/Taywee/args) parses the server command-line arguments.

### Compile CAPIO

```bash
git clone https://github.com/High-Performance-IO/capio.git capio && cd capio
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

### CMake options

| Option                                     | Default | Description                                                  |
|--------------------------------------------|---------|--------------------------------------------------------------|
| `-DCAPIO_LOG=TRUE`                         | `FALSE` | Enable logging. Only takes effect in `Debug` builds          |
| `-DCAPIO_BUILD_TESTS=TRUE`                 | `FALSE` | Build the test suite                                         |
| `-DCAPIO_BUILD_SERVER=FALSE`               | `TRUE`  | Skip building `capio_server`                                 |
| `-DCAPIO_BUILD_POSIX=FALSE`                | `TRUE`  | Skip building `libcapio_posix.so`                            |
| `-DCAPIO_MEMORY_CHECKS=<full\|fast\|none>` | `full`  | How the server checks node memory before allocating buffers |

If a requested file buffer is larger than the memory reported by `CAPIO_MEMORY_CHECKS`, the allocation is capped to
that memory, and a warning is printed when it is less than 10% of the requested size:

- `full`: reads `MemAvailable` from `/proc/meminfo` (free memory plus reclaimable cache). This is the only way to know
  the memory that is actually allocatable.
- `fast`: reads only the free memory with `sysconf`, without file I/O. It ignores reclaimable cache, so allocations
  may be capped more than needed.
- `none`: disables the check.

### Documentation

The source code documentation can be generated with Doxygen, using the
[doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css) theme:

```bash
cd doxy
git clone --depth 1 https://github.com/jothepro/doxygen-awesome-css.git theme
doxygen Doxyfile
```

The HTML output is written to `doxy/html`.

## Using CAPIO

You don't need to modify your application code. Just follow these steps.

### 1. Write a configuration file

Write a CAPIO-CL configuration file to inject streaming into your workflow. Refer to the
[CAPIO-CL repository](https://github.com/High-Performance-IO/CAPIO-CL) and the
[CAPIO-CL docs](https://capio.hpc4ai.it/docs/coord-language/) for details. A minimal example:

```json
{
  "name": "my_workflow",
  "IO_Graph": [
    {
      "name": "writer",
      "input_stream": [],
      "output_stream": ["file0.dat", "file1.dat"],
      "streaming": [
        { "name": ["file0.dat"], "committed": "on_close" },
        { "name": ["file1.dat"], "committed": "on_termination" }
      ]
    },
    {
      "name": "reader",
      "input_stream": ["file0.dat", "file1.dat"],
      "output_stream": []
    }
  ]
}
```

### 2. Launch the CAPIO servers

Start one server per node. `CAPIO_DIR` is required and must be an existing directory: it is the CAPIO mount point.

```bash
CAPIO_DIR=your_capiodir capio_server -c conf.json
```

To start a server without a configuration file, pass `--no-config` instead of `-c`.

### 3. Launch your application

Set the environment variables and preload the `libcapio_posix.so` intercepting library:

```bash
CAPIO_DIR=your_capiodir      \
CAPIO_WORKFLOW_NAME=wfname   \
CAPIO_APP_NAME=appname       \
LD_PRELOAD=libcapio_posix.so \
./your_app <args>
```

> [!WARNING]
> If `CAPIO_DIR` is not set, the application runs without CAPIO intercepting its system calls. If
> `CAPIO_WORKFLOW_NAME` and `CAPIO_APP_NAME` are not set, they default to `CAPIO` and `default_app`, which will not
> match the steps described in your CAPIO-CL configuration file.

To shut down a server, send it `SIGTERM` or `SIGINT` (e.g. `killall capio_server`). The server releases its resources
and exits.

## Server options

| Option                            | Description                                                                         |
|-----------------------------------|-------------------------------------------------------------------------------------|
| `-c, --config FILE`               | CAPIO-CL configuration file                                                         |
| `--no-config`                     | Start without a configuration file, using default settings                          |
| `-r, --resolve-prefix PREFIX`     | Prefix used to resolve relative paths in the configuration file                     |
| `--mem-only`                      | Store all files in memory, regardless of the configuration file                     |
| `-b, --backend NAME`              | Communication backend (see below)                                                   |
| `--backend-options OPTS`          | Backend-specific options (see below)                                                |
| `--discovery mcast\|fs`           | How servers find each other: multicast (default) or token files on a shared filesystem |
| `--mcast-addr ADDR`               | Multicast discovery address (default `224.0.0.2`)                                   |
| `--mcast-port PORT`               | Multicast discovery port (default `22334`)                                          |
| `--token-directory DIR`           | Token directory for `fs` discovery (default `.capio_tokens/`)                       |
| `--location-query-timeout MS`     | How long a file lookup waits for the other servers to answer (default 5000)         |
| `--location-requery-interval MS`  | How often a server waiting for a file to be created asks again (default 500)        |
| `--continue-on-error`             | Try to keep running after a fatal error. Only for debugging: behaviour is undefined |

### Backends

| Backend   | Description                                                      |
|-----------|------------------------------------------------------------------|
| `none`    | Default. No communication between servers                        |
| `mtcl`    | Communication through MTCL                                       |
| `mpi`     | Communication through MPI (only if built with MPI)               |
| `mpisync` | Like `mpi`, but with blocking receives (only if built with MPI)  |

Use `--backend-options` to configure the MTCL backend. Its format is `PROTO:PORT@POLL_INTERVAL_US`, for example:

```bash
capio_server --no-config --backend mtcl --backend-options TCP:7600@1000000
```

If omitted, MTCL defaults to `TCP:7600@1000000`. See the [MTCL repository](https://github.com/ParaGroup/MTCL) for
supported protocols and their requirements.

## Environment variables

### Server and application

| Variable                | Description                                                                |
|-------------------------|----------------------------------------------------------------------------|
| `CAPIO_DIR`             | CAPIO mount point, shared by servers and applications                      |
| `CAPIO_CACHE_LINES`     | Number of cache lines between application and server (default: 10)         |
| `CAPIO_CACHE_LINE_SIZE` | Size of a cache line, in bytes (default: 256KB)                            |
| `CALF_LOG_DIR`          | Directory for log files (default: `./capio_logs`). Needs `CAPIO_LOG`        |
| `CALF_LOG_PREFIX`       | Log file name prefix. Needs `CAPIO_LOG`                                     |

### Server only

| Variable                   | Description                                                              |
|----------------------------|--------------------------------------------------------------------------|
| `CAPIO_FILE_INIT_SIZE`     | Bytes preallocated for each new file (default: 4GB)                      |
| `CAPIO_PREFETCH_DATA_SIZE` | If set, data transfers between servers are at least this many bytes      |

### Application only

| Variable              | Description                                                          |
|-----------------------|----------------------------------------------------------------------|
| `CAPIO_WORKFLOW_NAME` | Must match the `"name"` field of the configuration file             |
| `CAPIO_APP_NAME`      | Must match the `"name"` of the step in the configuration `IO_Graph` |

## Examples

[High-Performance-IO/IO-pattern-benchmarks](https://github.com/High-Performance-IO/IO-pattern-benchmarks) contains
producer, consumer and producer-consumer applications that exercise different file access patterns and workflow
topologies, with a CAPIO-CL configuration and a script to run them on CAPIO.
More documentation is available at [https://capio.hpc4ai.it/docs](https://capio.hpc4ai.it/docs).

## Report bugs and get help

- [Create an issue](https://github.com/High-Performance-IO/capio/issues/new)
- [Official documentation](https://capio.hpc4ai.it/docs)

## CAPIO team

- Marco Edoardo Santimaria – <marcoedoardo.santimaria@unito.it> (Designer & Maintainer)
- Iacopo Colonnelli – <iacopo.colonnelli@unito.it> (Workflow Support & Maintainer)
- Massimo Torquati – <massimo.torquati@unipi.it> (Designer)
- Marco Aldinucci – <marco.aldinucci@unito.it> (Designer)

Former members:

- Alberto Riccardo Martinelli – <albertoriccardo.martinelli@unito.it> (Designer & Maintainer)

## Publications

- [![CAPIO methodology](https://img.shields.io/badge/CAPIO%20Methodology%20(Use%20this%20to%20cite%20CAPIO)-10.1016/j.future.2025.108159-%23cc5500?logo=doi&logoColor=white&labelColor=2b2b2b)](http://dx.doi.org/10.1016/j.future.2025.108159)
- [![CAPIO middleware](https://img.shields.io/badge/HiPC%202023%20Paper-10.1109/HiPC58850.2023.00031-%23cc5500?logo=doi&logoColor=white&labelColor=2b2b2b)](http://dx.doi.org/10.1109/HiPC58850.2023.00031)
- [![CAPIO-CL](https://img.shields.io/badge/CAPIO--CL-10.1007/s10766--025--00789--0-%23cc5500?logo=doi&logoColor=white&labelColor=2b2b2b)](https://doi.org/10.1007/s10766-025-00789-0)

# CAPIO

CAPIO (Cross-Application Programmable I/O), is a middleware aimed at injecting streaming capabilities to workflow steps
without changing the application codebase. It has been proven to work with C/C++ binaries, Fortran Binaries, JAVA,
python and bash.

[![codecov](https://codecov.io/gh/High-Performance-IO/capio/graph/badge.svg?token=6ATRB5VJO3)](https://codecov.io/gh/High-Performance-IO/capio)
![CI-Tests](https://github.com/High-Performance-IO/capio/actions/workflows/ci-tests.yaml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://raw.githubusercontent.com/High-Performance-IO/capio/master/LICENSE)

> [!IMPORTANT]  
> This version of CAPIO does not support writes to memory.
> If you need it please refer to releases/v1.0.0

## Build and run tests

### Dependencies

CAPIO depends on the following software that needs to be manually installed:

- `cmake >=3.15`
- `c++17` or newer
- `pthreads`

The following dependencies are automatically fetched during cmake configuration phase, and compiled when required.

- [syscall_intercept](https://github.com/pmem/syscall_intercept) to intercept syscalls
- [Taywee/args](https://github.com/Taywee/args) to parse server command line inputs
- [simdjson/simdjson](https://github.com/simdjson/simdjson) to parse json configuration files

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

1) Write a configuration file for injecting streaming capabilities to your workflow

2) Launch the CAPIO daemons with MPI passing the (eventual) configuration file as argument on the machines in which you
   want to execute your program (one daemon for each node). If you desire to specify a custom folder
   for capio, set `CAPIO_DIR` as a environment variable.
   ```bash
   [CAPIO_DIR=your_capiodir] capio_server -c conf.json 
   ```

> [!NOTE]
> if `CAPIO_DIR` is not specified when launching capio_server, it will default to the current working directory of
> capio_server.

3) Launch your programs preloading the CAPIO shared library like this:
   ```bash
   CAPIO_DIR=your_capiodir      \
   CAPIO_WORKFLOW_NAME=wfname   \ 
   CAPIO_APP_NAME=appname       \
   LD_PRELOAD=libcapio_posix.so \ 
   ./your_app <args>
    ```

> [!WARNING]  
> `CAPIO_DIR` must be specified when launching a program with the CAPIO library. if `CAPIO_DIR` is not specified, CAPIO
> will not intercept syscalls.

### Available environment variables

CAPIO can be controlled through the usage of environment variables. The available variables are listed below:

#### Global environment variable

- `CAPIO_DIR` This environment variable tells to both server and application the mount point of capio;
- `CAPIO_LOG_LEVEL` this environment tells both server and application the log level to use. This variable works only
  if `-DCAPIO_LOG=TRUE` was specified during cmake phase;
- `CAPIO_LOG_PREFIX` This environment variable is defined only for capio_posix applications and specifies the prefix of
  the logfile name to which capio will log to. The default value is `posix_thread_`, which means that capio will log by
  default to a set of files called `posix_thread_*.log`. An equivalent behaviour can be set on the capio server using
  the `-l` option;
- `CAPIO_LOG_DIR` This environment variable is defined only for capio_posix applications and specifies the directory
  name to which capio will be created. If this variable is not defined, capio will log by default to `capio_logs`. An
  equivalent behaviour can be set on the capio server using the `-d` option;
- `CAPIO_CACHE_LINE_SIZE`: This environment variable controls the size of a single cache line. defaults to 256KB;

#### Server only environment variable

- `CAPIO_METADATA_DIR`: This environmental variable controls the location of the metadata files used by CAPIO. it
  defaults to CAPIO_DIR. BE CAREFUL to put this folder on a path that is accessible by all instances of the running
  CAPIO servers.

#### Posix only environment variable

> [!WARNING]  
> The following variables are mandatory. If not provided to a posix, application, CAPIO will not be able to correctly
> handle the
> application, according to the specifications given from the json configuration file!

- `CAPIO_WORKFLOW_NAME`: This environment variable is used to define the scope of a workflow for a given step. Needs to
  be the same one as the field `"name"` inside the json configuration file;
- `CAPIO_APP_NAME`: This environment variable defines the app name within a workflow for a given step;

## How to inject streaming capabilities into your workflow

You can find documentation as well as examples, on the official documentation page at 

[Official documentation website](https://capio.hpc4ai.it/docs) 


## Report bugs + get help

[Create a new issue](https://github.com/High-Performance-IO/capio/issues/new)

[Get help](capio.hpc4ai.it/docs)


## CAPIO Team

Made with :heart: by:

Marco Edoardo Santimaria <marcoedoardo.santimaria@unito.it> (Designer and maintainer) \
Iacopo Colonnelli <iacopo.colonnelli@unito.it> (Workflows expert and maintainer) \
Massimo Torquati <massimo.torquati@unipi.it> (Designer) \
Marco Aldinucci <marco.aldinucci@unito.it> (Designer)

Former members:

Alberto Riccardo Martinelli <albertoriccardo.martinelli@unito.it> (designer and maintainer up to release/v1.0.0) 

## Papers

[![CAPIO](https://img.shields.io/badge/CAPIO-10.1109/HiPC58850.2023.00031-red)]([https://arxiv.org/abs/2206.10048](https://dx.doi.org/10.1109/HiPC58850.2023.00031))



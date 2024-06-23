# CAPIO

CAPIO (Cross-Application Programmable I/O), is a middleware aimed at injecting streaming capabilities to workflow steps
without changing the application codebase. It has been proven to work with C/C++ binaries, Fortran Binaries, JAVA,
python and bash. 

[![codecov](https://codecov.io/gh/High-Performance-IO/capio/graph/badge.svg?token=6ATRB5VJO3)](https://codecov.io/gh/High-Performance-IO/capio)
![CI-Tests](https://github.com/High-Performance-IO/capio/actions/workflows/ci-tests.yaml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://raw.githubusercontent.com/High-Performance-IO/capio/master/LICENSE)

## Build and run tests

### Dependencies

CAPIO depends on the following software that needs to be manually installed:

- `cmake >=3.15`
- `c++17` or newer
- `openmpi`
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
   [CAPIO_DIR=your_capiodir] [mpiexec -N 1 --hostfile your_hostfile] capio_server -c conf.json 
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
- `CAPIO_CACHE_LINES`: This environment variable controls how many lines of cache are presents between posix and server
  applications. defaults to 10 lines;
- `CAPIO_CACHE_LINE_SIZE`: This environment variable controls the size of a single cache line. defaults to 256KB;

#### Server only environment variable

- `CAPIO_FILE_INIT_SIZE`: This environment variable defines the default size of pre allocated memory for a new file
  handled by capio. Defaults to 4MB. Bigger sizes will reduce the overhead of malloc but will fill faster node memory.
  Value has to be expressed in bytes;
- `CAPIO_PREFETCH_DATA_SIZE`: If this variable is set, then data transfers between nodes will be always, at least of the
  given value in bytes;

#### Posix only environment variable

> [!WARNING]  
> The following variables are mandatory. If not provided to a posix, application, CAPIO will not be able to correctly
> handle the
> application, according to the specifications given from the json configuration file!

- `CAPIO_WORKFLOW_NAME`: This environment variable is used to define the scope of a workflow for a given step. Needs to
  be the same one as the field `"name"` inside the json configuration file;
- `CAPIO_APP_NAME`: This environment variable defines the app name within a workflow for a given step;

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

Alberto Riccardo Martinelli <albertoriccardo.martinelli@unito.it> (designer and maintainer) \
Marco Edoardo Santimaria <marcoedoardo.santimaria@unito.it> (Designer and maintainer) \
Iacopo Colonnelli <iacopo.colonnelli@unito.it> (Workflows expert and maintainer) \
Massimo Torquati <massimo.torquati@unipi.it> (Designer) \
Marco Aldinucci <marco.aldinucci@unito.it> (Designer)

# CAPIO

Cross-Application Programmable I/O. High-performance support for direct multi-rail, programmable and portable streaming
across different distributed applications (e.g. MPI-app1 -> MPI-app2).

## Report bugs + get help

[Create a new issue](https://github.com/High-Performance-IO/capio/issues/new)

[Get help](https://github.com/High-Performance-IO/capio/wiki)

> [!TIP]
> A [wiki](https://github.com/High-Performance-IO/capio/wiki) is in development! You might want to check the wiki to get more in depth informations about CAPIO!

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
   [CAPIO_DIR=your_capiodir] mpiexec -N 1 --hostfile your_hostfile capio_server -c conf.json 
   ```

> [!NOTE]
> if `CAPIO_DIR` is not specified when launching capio_server, it will default to the current working directory of
> capio_server.

3) Launch your programs preloading the CAPIO shared library like this:
   ```bash
   CAPIO_DIR=your_capiodir LD_PRELOAD=libcapio_posix.so ./your_app args
    ```

> [!WARNING]  
> `CAPIO_DIR` must be specified when launching a program with the CAPIO library. if `CAPIO_DIR` is not specified, CAPIO
> will not intercept syscalls.

### Available environment variables

- `CAPIO_DIR` This environment variable tells to both server and application the mount point of capio
- `CAPIO_LOG_LEVEL` this environment tells both server and application the log level to use. This variable works only
  if `-DCAPIO_LOG=TRUE` was specified during cmake phase.
- `CAPIO_LOGFILE` This environment variable is defined only for capio_posix applications and specifies the logfile name
  to which capio will log to. If this variable is not defined, capio will log by default to `posix_thread_*.log`. An equivalent is available on capio server with option `-l`
  - `CAPIO_LOGDIR` This environment variable is defined only for capio_posix applications and specifies the directory name
  to which capio will log to. If this variable is not defined, capio will log by default to `capio_logs`. An equivalent is available on capio server with option `-d`

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
          "name": "file0.dat",
          "committed": "on_close"
        },
        {
          "name": "file1.dat",
          "committed": "on_close",
          "mode": "no_update"
        },
        {
          "name": "file2.dat",
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

## CAPIO Team

Made with :heart: by:

Alberto Riccardo Martinelli <albertoriccardo.martinelli@unito.it> (Designer and maintainer)\
Marco Edoardo Santimaria <marcoedoardo.santimaria@unito.it> (Designer and mantainer) \
Iacopo Colonnelli <iacopo.colonnelli@unito.it> (Workflows expert and mantainer)\
Massimo Torquati <massimo.torquati@unipi.it> (Designer)\
Marco Aldinucci <marco.aldinucci@unito.it> (Designer)


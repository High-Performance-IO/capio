# CAPIO: Cross Application Programmable IO

CAPIO is a middleware aimed at injecting streaming capabilities into workflow steps
without changing the application codebase. It has been proven to work with C/C++ binaries, Fortran, Java, Python, and
Bash.

[![codecov](https://codecov.io/gh/High-Performance-IO/capio/graph/badge.svg?token=6ATRB5VJO3)](https://codecov.io/gh/High-Performance-IO/capio) ![CI-Tests](https://github.com/High-Performance-IO/capio/actions/workflows/ci-tests.yaml/badge.svg) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://raw.githubusercontent.com/High-Performance-IO/capio/master/LICENSE)

> [!TIP]
> CAPIO is now multibackend and dynamic by nature: you do not need MPI, to benefit for the in-memory IO improvements!
> Just use a MTCL provided backend, if you want the in-memory IO, or fall back to the file system backend (default) if
> oy just want to coordinate IO operations between workflow steps!


---
## Automatic install with SPACK

CAPIO is on SPACK! to install it automatically, just add the High Performance IO 
repo to spack and then install CAPIO:
```bash
spack repo add https://github.com/High-Performance-IO/hpio-spack.git
spack install capio
```

## 🔧 Manual Build and Install

### Dependencies

**Required manually:**

- `cmake >= 3.15`
- `C++20`
- `pthreads`

**Fetched/compiled during configuration:**

- [syscall_intercept](https://github.com/pmem/syscall_intercept) - Intercept and handles LINUX system calls
- [Taywee/args](https://github.com/Taywee/args) - Parse user input arguments
- [simdjson/simdjson](https://github.com/simdjson/simdjson) - Parse fast JSON files
- [MTCL](https://github.com/ParaGroup/MTCL) - Provides abstractions over multiple communication backends

### Compile CAPIO

```bash
git clone https://github.com/High-Performance-IO/capio.git capio && cd capio
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

To enable logging support, pass `-DCAPIO_LOG=TRUE` during the CMake configuration phase.

---

## 🧑‍💻 Using CAPIO in Your Code

Good news! You **don’t need to modify your application code**. Just follow these steps:

### 1. Create a Configuration File *(optional but recommended)*

Write a CAPIO-CL configuration file to inject streaming into your workflow. Refer to
the [CAPIO-CL Docs](https://capio.hpc4ai.it/docs/coord-language/) for details.

### 2 Launch the workflow with CAPIO

To launch your workflow with capio you can follow two routes:

#### A) Use `capiorun` for simplified operations

You can simplify the execution of workflow steps with CAPIO using the `capiorun` utility. See the
[`capiorun` documentation](capiorun/readme.md) for usage and examples. `capiorun` provides an easier way to manage
daemon startup and environment preparation, so that the user do not need to manually prepare the environment.

#### B) Manually launch CAPIO

Launch the CAPIO Daemons: start one daemon per node. Optionally set `CAPIO_DIR` to define the CAPIO mount point:

```bash
[CAPIO_DIR=your_capiodir] capio_server -c conf.json
```

> [!CAUTION]
> If `CAPIO_DIR` is not set, it defaults to the current working directory.

You can now start your application. Just set the right environment variable and remember to set `LD_PRELOAD` to the
`libcapio_posix.so` intercepting library:

```bash
CAPIO_DIR=your_capiodir
CAPIO_WORKFLOW_NAME=wfname
CAPIO_APP_NAME=appname
LD_PRELOAD=libcapio_posix.so
./your_app <args>

killall -USR1 capio_server
```

> [!CAUTION]  
> if `CAPIO_APP_NAME` and `CAPIO_WORKFLOW_NAME` are not set (or are set but do not match the values present in the
> CAPIO-CL configuration file), CAPIO will not be able to operate correctly!

> [!tip]  
> To gracefully shut down the capio server instance, just send the SIGUSR1 signal.
> the capio_server process will then automatically clean up and terminate itself!

---

## ⚙️ Environment Variables

### 🔄 Global

| Variable                | Description                                        |
|-------------------------|----------------------------------------------------|
| `CAPIO_DIR`             | Shared mount point for server and application      |
| `CAPIO_LOG_LEVEL`       | Logging level (requires `-DCAPIO_LOG=TRUE`)        |
| `CAPIO_LOG_PREFIX`      | Log file name prefix (default: `posix_thread_`)    |
| `CAPIO_LOG_DIR`         | Directory for log files (default: `capio_logs`)    |
| `CAPIO_CACHE_LINE_SIZE` | Size of a single CAPIO cache line (default: 256KB) |

### 🖥️ Server-Only

| Variable             | Description                                                                |
|----------------------|----------------------------------------------------------------------------|
| `CAPIO_METADATA_DIR` | Directory for metadata files. Defaults to `CAPIO_DIR`. Must be accessible. |

### 📁 POSIX-Only (Mandatory)

> ⚠️ These are required by CAPIO-POSIX. Without them, your app will not behave as configured in the JSON file.

| Variable              | Description                                     |
|-----------------------|-------------------------------------------------|
| `CAPIO_WORKFLOW_NAME` | Must match `"name"` field in your configuration |
| `CAPIO_APP_NAME`      | Name of the step within your workflow           |

---

## 📖 Extended documentation

Documentation and examples are available on the official site:

🌐 [https://capio.hpc4ai.it/docs](https://capio.hpc4ai.it/docs)

---

## 🐞 Report Bugs & Get Help

- [Create an issue](https://github.com/High-Performance-IO/capio/issues/new)
- [Official Documentation](https://capio.hpc4ai.it/docs)

---

## 👥 CAPIO Team

Made with ❤️ by:

- Marco Edoardo Santimaria – <marcoedoardo.santimaria@unito.it> (Designer & Maintainer)
- Iacopo Colonnelli – <iacopo.colonnelli@unito.it> (Workflow Support & Maintainer)
- Massimo Torquati – <massimo.torquati@unipi.it> (Designer)
- Marco Aldinucci – <marco.aldinucci@unito.it> (Designer)

**Former Members:**

- Alberto Riccardo Martinelli – <albertoriccardo.martinelli@unito.it> (Designer & Maintainer)

---

## 📚 Publications

[![CAPIO](https://img.shields.io/badge/CAPIO-10.1109/HiPC58850.2023.00031-red)](https://dx.doi.org/10.1109/HiPC58850.2023.00031)

[![](https://img.shields.io/badge/CAPIO--CL-10.1007%2Fs10766--025--00789--0-green?style=flat&logo=readthedocs)](https://doi.org/10.1007/s10766-025-00789-0)
# CAPIO on Docker

Even if CAPIO has been primarily designed for bare metal, high-performance execution environments, [Docker](https://www.docker.com/) containers can be used to set up a simulated distributed environment on a local development machine. This example shows how to set up a minimal environment using CAPIO standard Docker images. Make sure you correctly installed and configured Docker on your local environment (see [here](https://docs.docker.com/engine/install/)).

### Start the execution environment

A `docker-compose.yml` file is provided in the current folder. By default, it starts `2` container instances of the CAPIO standard image, called `hpio/capio:latest`. Note that both images mount a `./server` directory from the host in the `/home/capio/server` folder on the container file system. Due to the way CAPIO servers communicate, the `/home/capio/server` folder must be shared between all CAPIO instances. To start the environment, use the following command

```bash
docker compose -p example up -d 
```

This command will start two Docker containers called `example-capio-1` and `example-capio-2`. You should be able to see them up and running by using the following command

```bash
docker compose -p example ps
```

which should print something like this

```
NAME              IMAGE               COMMAND               SERVICE   CREATED         STATUS         PORTS
example-capio-1   hpio/capio:latest   "/usr/sbin/sshd -D"   capio     5 seconds ago   Up 5 seconds   22/tcp
example-capio-2   hpio/capio:latest   "/usr/sbin/sshd -D"   capio     5 seconds ago   Up 5 seconds   22/tcp
```

### Start the CAPIO server

Now it's time to start CAPIO using MPI. The first step is to correctly configure your MPI `hostfile`. You can generate a valid `hostfile` with the following command

```bash
docker compose -p example ps --format '{{.Names}}' > hostfile
```

Then you can copy it inside the first container instance, called `example-capio-1`, as follows

```bash
docker compose -p example cp --index 1 ./hostfile capio:/home/capio/
```

Note that you are transferring the file in the home directory of the `capio` user. It is a good practice to avoid using the privileged `root` user whenever possible when working with Docker containers. If the command succeeds, it should print something like this

```
[+] Copying 1/0
 ✔ example-capio-1 copy ./hostfile to example-capio-1:/home/capio/ Copied 
```

The last preliminary step is to create the `CAPIO_DIR` on every node. For this example, we set it to `/tmp/capio`. The following command creates a `/tmp/capio` directory on all CAPIO instances using MPI

```bash
docker compose -p example exec --user capio --index 1 capio \
  mpirun -N 1 --hostfile /home/capio/hostfile mkdir -p /tmp/capio
```

Note that the command requires MPI to execute one process per node using the `-N 1` option of the `mpirun` command, and specifies the nodes' hostnames through the `hostfile` you just generated.

Finally, the CAPIO server can be started in background using the following command

```bash
docker compose -p example exec                \
  --detach                                    \
  --index 1                                   \
  --env CAPIO_DIR=/tmp/capio                  \
  --env CAPIO_LOG_LEVEL=-1                    \
  --user capio                                \
  --workdir /home/capio/server                \
  capio                                       \
  sh -c 'mpirun                               \
  -N 1                                        \
  --hostfile /home/capio/hostfile             \
  -x CAPIO_DIR                                \
  -x CAPIO_LOG_LEVEL                          \
  capio_server --no-config > server.out 2>&1'
```

Let's examine some of the options introduced in the previous command. The `--detach` option allows to run a command in background. The `--env` option adds environment variables to the target container instance, in this case the one called `example-capio-1`. The `-x` option of the `mpirun` command propagates the specified list of environment variables to all nodes it targets. Finally, the `--workdir` option specifies that the CAPIO server should use the shared `/home/capio/server` directory as the working directory, to store logs and other configuration files.

### Start the CAPIO application

To run a CAPIO application it is necessary to preload the `libcapio_posix.so` library through the `LD_PRELOAD` environment variable. This operation can be easily achieved using the `docker compose exec` action as follows

```bash
docker compose -p example exec        \
  --index 1                           \
  --env CAPIO_DIR=/tmp/capio          \
  --env LD_PRELOAD=libcapio_posix.so  \
  --user capio                        \
  capio                               \
  touch /tmp/capio/test_file.txt
```

The previous command creates an empty file called `test_file.txt` inside the `CAPIO_DIR` of the first CAPIO instance. Now it should be possible to see the file just created by executing an `ls` command on the other CAPIO instance, as follows

```bash
docker compose -p example exec        \
  --index 2                           \
  --env CAPIO_DIR=/tmp/capio          \
  --env LD_PRELOAD=libcapio_posix.so  \
  --user capio                        \
  capio                               \
  ls -la /tmp/capio/test_file.txt
```

If the command succeeds, it should print the following line

```
-rw-r--r-- 1 capio capio 0 Jan  1  1970 /tmp/capio/test_file.txt
```

### Shut down the environment

The following command can be used to shut down the Docker Compose environment

```bash
docker compose -p example down
```

If the command succeeds, it should print something like this

```
[+] Running 3/3
 ✔ Container example-capio-2  Removed  0.3s 
 ✔ Container example-capio-1  Removed  0.4s 
 ✔ Network example_capionet   Removed  0.3s 
```


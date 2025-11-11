```shell

           _         _      _          _      _        
          /\ \      /\_\   /\ \    _ / /\    / /\      
         /  \ \    / / /  _\ \ \  /_/ / /   / /  \     
        / /\ \ \  / / /  /\_\ \ \ \___\/   / / /\ \__  
       / / /\ \_\/ / /__/ / / / /  \ \ \  / / /\ \___\ 
      / /_/_ \/_/ /\_____/ /\ \ \   \_\ \ \ \ \ \/___/ 
     / /____/\ / /\_______/  \ \ \  / / /  \ \ \       
    / /\____\// / /\ \ \      \ \ \/ / /    \ \ \      
   / / /     / / /  \ \ \      \ \ \/ /_/\__/ / /      
  / / /     / / /    \ \ \      \ \  /\ \/___/ /       
  \/_/      \/_/      \_\_\      \_\/  \_____\/
  
```

[![CMake on macOS & Ubuntu](https://github.com/AlexJuca/fkvs/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/AlexJuca/fkvs/actions/workflows/cmake-multi-platform.yml)

⚡ FKVS (Fast Key Value Store) is a tiny, **high‑performance** key‑value store written in C
with a single‑threaded, non‑blocking I/O multiplexed event loop.

It is part of my experiment to understand what it takes to build a key value store similar to redis in C from first
principles.

## Running the fkvs server inside docker

```shell
docker build -t fkvs:latest -f Dockerfile .  

docker run --rm -it --name fkvs -p 5995:5995 fkvs:latest # if you intend to connecting from host via tcp
docker run --rm -it --name fkvs fkvs:latest

## Connect to server using 127.0.0.1 and port 5995
docker exec -it fkvs /usr/local/bin/fkvs-cli -h 127.0.0.1 -p 5995 -d /home/fkvs/client.conf

## Additional commands for running benchmarks from within the container
docker exec -it fkvs /usr/local/bin/fkvs-benchmark -n 1000000 -t set -c 30 -u # run benchmark using unix domain sockets
docker exec -it fkvs /usr/local/bin/fkvs-benchmark -n 1000000 -t set -c 30 # run benchmark using tcp
```

## Build fkvs from source

### Build and run fkvs on Ubuntu 20+

```shell
sudo apt remove --purge --auto-remove cmake
sudo apt update
sudo apt install -y software-properties-common lsb-release && \
sudo apt clean all
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
sudo apt-add-repository "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main"
sudo apt update
sudo apt install kitware-archive-keyring
rm /etc/apt/trusted.gpg.d/kitware.gpg
sudo apt update
sudo apt install cmake

make -f Makefile.fkvs setup-and-build

./fkvs-server -c
```

### Build and run fkvs on macOS 13 +

```shell
brew install cmake

make -f Makefile.fkvs setup-and-build

./fkvs-server -c
```

### Running the client

```shell
$ ./fkvs-cli
Connected to server on port 5995
Type 'exit' to quit.
127.0.0.1:5995> PING google.com
"google.com"
```

### Running the client in non-interactive mode

```shell
$ ./fkvs-cli -h 127.0.0.1 -p 5995 --non-interactive
"PONG"
```

### Supported commands

- PING | PING "value"
- SET key value
- GET key
- INCR key
- INCRBY key value
- DECR key
- INFO 


## Benchmarking

For simple micro benchmarking, use the fkvs-benchmark CLI utility to test
the server's throughput.

Throughput in FKVS is measured as completed request–response cycles: a client sends a request using our binary protocol,
the server validates and executes it, sends back an acknowledgment, and only then is it counted as one request.

This means we're measuring end-to-end latency per request, not just how fast the server can parse or execute commands.

Below is a list of currently supported arguments you can use for benchmarking.

```text
Usage: fkvs-benchmark [-n total_requests] [-c clients] [-h host] [-p port] 
          [-k]
          -n N     total requests across all clients (default 100000)
          -c C     number of concurrent clients (default 32)
          -h HOST  server host/IP (default 127.0.0.1)
          -k       keep-alive (default on)
          -u       connect via unix domain socket
          -t       type of command to use during benchmark (ping,
          set, default ping)
          -r       use random non-pregenerated keys for all insertion commands (set, setx, etc)
```

You need to have a running fkvs server instance before launching the benchmark. A typical example would be:

Start the server:
```shell
./fkvs-server -c
```

Send N requests to the server.
```shell
./fkvs-benchmark -q -n 10000
```

The output:
```shell
32/32 workers ready.
Clients: 32  Total requests: 10000
Completed: 10000  Failed: 0
Elapsed: 0.089054 s  Throughput: 112290.95 req/s
```

You can stress cache misses and in general to simulate a more real-world work load by using a large key space. A random key 
is generated for every request such that each request for commands that write to the database, e.g, set, has
a unique string as the key.

```shell
./fkvs-benchmark -r -n 1000000 -c 25 -t set
```
The output:
```shell
25/25 workers ready.
Clients: 25  Total requests: 1000000
Completed: 1000000  Failed: 0
Elapsed: 13.417809 s  Throughput: 74527.82 req/s
```

By default, the server and client communicate via TCP/IP on port 5995, but you can perform micro benchmarks 
locally using unix domain sockets.

Ensure the server is running in unix mode by updating the config to something like this:

```text
# Server configuration
logs-enabled false
verbose false
daemonize false
show-logo true
unixsocket /tmp/fkvs.sock
use-io-uring false
```

Start the server:
```shell
./fkvs-server -c
```

and use the `-u` argument when invoking fkvs-benchmark to ensure we connect via unix domain sockets:

```shell
./fkvs-benchmark -u -n 1000000 -c 25 -t set
```

This should generally result in faster command execution due to no TCP/IP communication overhead.

```shell
25/25 workers ready.
Clients: 25  Total requests: 1000000
Completed: 1000000  Failed: 0
Elapsed: 3.263411 s  Throughput: 306427.84 req/s
```

## Event Dispatchers Overview

fkvs supports multiple event dispatchers, leveraging the available I/O technologies available on
the host operating system for the best possible I/O performance.

Below, we review the different type `event_dispatcher_kind`'s that fkvs supports and their characteristics.

### Reactive-based Dispatchers
These are readiness-driven mechanisms: the kernel notifies the fkvs process when a file descriptor 
becomes ready for I/O (readable or writable).

The server then performs the actual I/O operation by calling the necessary handler.

kqueue:	(macOS -> BSD-style event notification system. Efficient for large numbers of connections)

epoll:	(Linux -> Edge-triggered or level-triggered I/O multiplexer optimized for high concurrency)

In both cases, fkvs waits for readiness notifications and then executes the corresponding read/write 
operations, meaning a syscall still happens per I/O.

### Pro-reactive-based Dispatcher
This is an operation-driven mechanism: The fkvs server process submits actual I/O requests to the kernel, and 
the kernel completes them asynchronously, returning results later via a completion queue.

io_uring: (Linux kernel ≥ 5.6, Enables asynchronous, batched, and zero-syscall I/O via shared ring buffers between user
and kernel space. Provides significantly higher throughput for small, frequent operations)

Using `io_uring` generally leads to fewer syscalls (batch submission), lower context-switch overhead, 
improved cache locality, better CPU efficiency, lower tail latency under load.

### Configuration
To enable `io_uring` on Linux, set the following option in your server.conf file:

```server.conf
# Enable io_uring for pro-reactive I/O handling on Linux
use-io-uring true
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

Copyright (c) 2025 Alexandre Juca - <corextechnologies@gmail.com>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


## What is FKVS?

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

FKVS (Fast Key Value Store) is a tiny, high‑performance key‑value store written in C (with no dependencies) with a single‑threaded, non‑blocking event loop.

It is part of my experiment to understand what it takes to build a key value store similar to redis in C from first principles.
I am not a C programmer by profession, so the code in this project is based on my limited understanding of the C language.

## High Level Design
FKVS is composed of a server and client application.
- The server listens for requests and processes commands from clients.
- The client exposes a REPL where users can dispatch commands for the server to execute.

The server uses a single-threaded event loop with I/O multiplexing for command execution using Kqueue on macOS and epoll 
on Linux.

The server and client communicate using a custom binary protocol called a frame.

Frames ensure communication between the client and server is efficient and binary-safe.

FKVS executes commands atomically. The server is a single-threaded, non-blocking event loop; it reads a full frame, 
dispatches one handler, and completes it before handling the next. 

This guarantees:
- No interleaving of state mutations between commands from any clients.
- Pipelined commands are processed in order.
- Effects of a completed command are visible to subsequent commands.

## Running inside docker

```shell
docker build -t fkvs:latest -f Dockerfile .  

docker run --rm -it -p 5995:5995 fkvs:latest
```

## Build Fkvs from source

### Build and run Fkvs on Ubuntu 20+

```shell
apt remove --purge --auto-remove cmake
apt update
apt install -y software-properties-common lsb-release && \
apt clean all
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null
apt-add-repository "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main"
apt update
apt install kitware-archive-keyring
rm /etc/apt/trusted.gpg.d/kitware.gpg
apt update
apt install cmake

cmake .
cmake --build .
./fkvs-server -c
```

### Build and run Fkvs on macOS 13 +

```shell
brew install cmake

cmake .

cmake --build .

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

### Supported commands

- PING | PING "value"
- SET key value
- GET key
- INCR key
- INCRBY key value


### Benchmarking

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
          -r       use random pregenerated keys for insertion commands
```

You need to have a running fkvs instance before launching the benchmark. A typical example would be:

```shell
./fkvs-benchmark -q -n 10000
```

The output should be something like:
```shell
32/32 workers ready.
Clients: 32  Total requests: 10000
Completed: 10000  Failed: 0
Elapsed: 0.089054 s  Throughput: 112290.95 req/s
```

You can stress cache misses and in general to simulate a more real-world work load by using a large key space. A random key 
is generated for every request such that each request has a unique string as the key.

```shell
./fkvs-benchmark -r -n 1000000 -c 25 -t set
```
The output should be something like:
```shell
25/25 workers ready.
Clients: 25  Total requests: 1000000
Completed: 1000000  Failed: 0
Elapsed: 13.417809 s  Throughput: 74527.82 req/s
```

By default, the server and client communicate via TCP/IP on port 5995, but you can perform micro benchmarks locally using unix
domain sockets.

Ensure the server is running in unix mode by updating the config to something like this:

```text
# Server configuration
logs-enabled false
verbose false
daemonize false
show-logo true
unixsocket /tmp/fkvs.sock
```

and use the -u argument when invoking fkvs-benchmark:

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


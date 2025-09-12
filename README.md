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

docker run -it -p 5995:5995 fkvs:latest /bin/sh   

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


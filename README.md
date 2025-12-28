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
principles and to eventually get it into a production ready state.

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
sudo apt install gcc
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


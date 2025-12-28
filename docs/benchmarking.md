# Benchmarking

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
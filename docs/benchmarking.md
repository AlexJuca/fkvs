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
          -r       use a unique key per insertion command (set, setx, etc) instead of reusing a fixed key
          -P N     pipeline N commands per batch (default 1, no pipelining)
          -R RATE  open-model: drive a constant RATE req/s and report
                   coordinated-omission-corrected latency (overrides -P)
```

## Throughput mode vs. open-model latency mode

By default the benchmark is **closed-loop**: each client sends, waits for the
reply, then sends again (optionally pipelined with `-P`). This measures peak
throughput, but it **hides tail latency** — when the server stalls, a closed-loop
client simply stops sending, so the requests that *would* have queued during the
stall are never measured (this is *coordinated omission*).

`-R RATE` switches to an **open-model** load: each client sends on a fixed
schedule regardless of when responses come back, and every request's latency is
measured from the time it *should* have been sent. Stalls therefore surface as a
growing backlog of late requests instead of being silently skipped. Latencies are
recorded in an HdrHistogram and reported as percentiles.

```shell
# Drive a constant 50k req/s of SET across 16 clients and report latency.
./fkvs-benchmark -h 127.0.0.1 -p 5995 -c 16 -t set -R 50000 -n 200000
```

```text
Open-model latency (coordinated-omission corrected):
  Target rate : 50000 req/s   Achieved: 49975 req/s
  Samples     : 200000
  p50         :      33.0 us
  p99         :     120.0 us
  p99.9       :    2238.0 us
  max         :    2550.0 us
  mean        :      46.6 us
```

Interpretation tips:
- If **Achieved** falls below **Target**, the server cannot sustain that rate —
  the percentiles will show an exploding tail (often into seconds). Find the
  highest rate at which p99 stays within your SLO; that is the honest capacity.
- Report throughput **at a stated latency bound** (e.g. "X req/s at p99 < 1 ms"),
  the way high-performance KV stores are benchmarked, rather than a bare peak.
- Run the client and server on separate machines / pinned to disjoint cores —
  co-locating them on a busy host inflates the tail with client-side scheduling.

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
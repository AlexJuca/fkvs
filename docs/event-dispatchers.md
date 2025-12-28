# Event Dispatchers Overview

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
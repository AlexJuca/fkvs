# Profiling fkvs-server

macOS provides powerful tools to profile applications via 
the [Instruments app](https://developer.apple.com/tutorials/instruments)

We use it to analyze the performance, resource usage, behavior of 
all fkvs binaries and detect memory leaks.

To collect tracing information specifically for memory leak detection via Instruments
it's required that fkvs be signed.

## Prerequisites

1) Ensure you're on a recent macOS (e.g., Sonoma 14 or Sequoia 15 as of November 2025) with Xcode 16+ installed.

2) Compile fkvs with debug symbols enabled:


To compile with debug symbols and code-sign the fkvs-server for profiling in Instruments.app, run the following command:

```shell
make -f Makefile.fkvs codesign-server
```

To stress-test the server during profiling and uncover memory leaks in the write path, run
the following command after you have started the profiling session:

```shell
repeat 10 ./fkvs-benchmark -n 1000000 -t set -c 10 -r
```

To stress-test the server during profiling and uncover memory leaks in the read path, run
the following command after you have started the profiling session:

```shell
repeat 10 ./fkvs-benchmark -n 1000000 -t get -c 10
```

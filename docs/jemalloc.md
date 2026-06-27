# Building fkvs with jemalloc

fkvs can link `fkvs-server` against [jemalloc](https://jemalloc.net/) instead of
the system allocator. jemalloc is linked **unprefixed**, so it transparently
overrides `malloc`/`free`/`realloc` — no source changes or call-site edits are
required.

**Defaults:** jemalloc is enabled **by default on Linux** and falls back to the
system allocator if the library is not installed (the build never fails on a
missing jemalloc). On other platforms (e.g. macOS) it is **opt-in / off by
default**. Either way, `FKVS_ENABLE_JEMALLOC` lets you force it on or off.

## When to use it

jemalloc is **not** a free win for every workload:

- **Read-heavy or update-in-place workloads** (e.g. `SET` reusing the same keys)
  see **no change** — those paths barely call `malloc`, so throughput stays
  CPU/parse-bound.
- **Allocation-heavy workloads** (many distinct keys, high insert/churn rates)
  benefit the most. In benchmarks of `SET` with random keys, jemalloc delivered
  **~45–55% higher throughput** with zero code changes.

Enable it for write-heavy, large-keyspace deployments; leave it off otherwise.

## Requirements

| Platform | Install jemalloc |
|---|---|
| Ubuntu/Debian | `sudo apt install libjemalloc-dev` |
| Fedora/RHEL | `sudo dnf install jemalloc-devel` |
| Arch | `sudo pacman -S jemalloc` |
| macOS (Homebrew) | `brew install jemalloc` |

The build looks for the `jemalloc` library via CMake's `find_library`. If
jemalloc is enabled but the library is not found, the build prints a status
message and falls back to the system allocator (it does **not** fail). On Linux,
installing `libjemalloc-dev` is therefore all that is needed to get jemalloc by
default.

## Build

The allocator is selected at configure time with the `FKVS_ENABLE_JEMALLOC`
CMake option.

### Using the Makefile shortcut

`Makefile.fkvs` defers to the CMake default (jemalloc on Linux, off elsewhere)
unless you set `JEMALLOC` explicitly:

```shell
make -f Makefile.fkvs setup-and-build            # Linux: jemalloc if installed
make -f Makefile.fkvs setup-and-build JEMALLOC=ON
make -f Makefile.fkvs setup-and-build JEMALLOC=OFF
```

### Linux

jemalloc is the default here — just install the library and build:

```shell
sudo apt install libjemalloc-dev          # or your distro's equivalent

cmake -S . -B .
cmake --build .
```

Pass `-DFKVS_ENABLE_JEMALLOC=OFF` to force the system allocator instead.

### macOS

Homebrew installs jemalloc under its own prefix (`/opt/homebrew` on Apple
Silicon, `/usr/local` on Intel), which CMake's `find_library` does not always
search by default. Point it at the prefix explicitly so the library is found
reliably:

```shell
brew install jemalloc

cmake -S . -B . -DFKVS_ENABLE_JEMALLOC=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix jemalloc)"
cmake --build .
```

On Intel Macs (`/usr/local`) the explicit prefix is usually unnecessary, but
passing it is harmless and always works.

> Note: `Makefile.fkvs setup-and-build` defaults to jemalloc off. Pass
> `JEMALLOC=ON` (see above) or run the `cmake` commands directly to enable it.

## Verify it is active

The server logs the active allocator at startup:

```
2026-06-27 18:51:25 - allocator: 5.3.0-0-g54eaed1d8b56b1aa528be3bdd1877e59c56fa90c
```

A system-allocator build instead prints:

```
2026-06-27 18:51:24 - allocator: system (libc malloc)
```

You can also confirm the library is linked and that jemalloc is managing
allocations:

```shell
# Linux: confirm the shared library is linked
ldd ./fkvs-server | grep jemalloc

# macOS: confirm the dylib is linked
otool -L ./fkvs-server | grep jemalloc

# Any platform: dump jemalloc's own stats at exit (proves it is in control)
MALLOC_CONF=stats_print:true ./fkvs-server -c server.conf
```

## Docker

The `Dockerfile` (Linux) installs jemalloc and builds with it by default,
exposing an `ENABLE_JEMALLOC` build arg (default `ON`):

```shell
# Default image — jemalloc
docker build -t fkvs:latest -f Dockerfile .

# System-allocator image
docker build --build-arg ENABLE_JEMALLOC=OFF -t fkvs:system -f Dockerfile .
```

## Disabling

To force the system allocator (e.g. on Linux, where jemalloc is the default):

```shell
cmake -S . -B . -DFKVS_ENABLE_JEMALLOC=OFF
cmake --build .
```

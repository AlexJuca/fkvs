# Building fkvs with jemalloc

fkvs can link `fkvs-server` against [jemalloc](https://jemalloc.net/) instead of
the system allocator. jemalloc is linked **unprefixed**, so it transparently
overrides `malloc`/`free`/`realloc` — no source changes or call-site edits are
required. The feature is **opt-in** and **off by default**; the default build
keeps using the platform's libc allocator.

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
`FKVS_ENABLE_JEMALLOC=ON` is set but the library is not found, configuration
fails with a clear error.

## Build

The allocator is selected at configure time with the `FKVS_ENABLE_JEMALLOC`
CMake option.

### Using the Makefile shortcut

`Makefile.fkvs` forwards a `JEMALLOC` variable to the CMake option (default
`OFF`), so the usual one-liner can enable it:

```shell
make -f Makefile.fkvs setup-and-build JEMALLOC=ON
```

### Linux

```shell
sudo apt install libjemalloc-dev          # or your distro's equivalent

cmake -S . -B . -DFKVS_ENABLE_JEMALLOC=ON
cmake --build .
```

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

The `Dockerfile` installs jemalloc and exposes an `ENABLE_JEMALLOC` build arg
(default `OFF`):

```shell
# Default image — system allocator
docker build -t fkvs:latest -f Dockerfile .

# jemalloc image
docker build --build-arg ENABLE_JEMALLOC=ON -t fkvs:jemalloc -f Dockerfile .
```

## Disabling

jemalloc is off by default. To explicitly reconfigure an existing build tree
back to the system allocator:

```shell
cmake -S . -B . -DFKVS_ENABLE_JEMALLOC=OFF
cmake --build .
```

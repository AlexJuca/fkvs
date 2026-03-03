# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FKVS (Fast Key Value Store) is a high-performance key-value store written in C (C23 standard) with a single-threaded, non-blocking I/O multiplexed event loop. Default port: 5995.

## Build Commands

```bash
# Full setup (submodules) and build
make -f Makefile.fkvs setup-and-build

# Build only (after initial setup)
make -f Makefile.fkvs build

# Run tests
make -f Makefile.fkvs tests

# Run a single test directly after building
./test_counter
./test_string_utils

# macOS code signing (needed for network entitlements)
make -f Makefile.fkvs codesign-server

# Run the server
./fkvs-server -c

# Run the CLI client
./fkvs-cli
```

Build uses CMake (minimum 3.31.2). The build output (executables) goes to the project root directory.

## Architecture

### Platform-Specific Event Dispatchers

The server uses a compile-time selected event loop based on the target platform:
- **macOS**: `src/io/event_dispatcher_kqueue.c` (kqueue)
- **Linux**: `src/io/event_dispatcher_epoll.c` (epoll) or `src/io/event_dispatcher_io_uring.c` (io_uring if liburing is available)

This is controlled in `CMakeLists.txt` via `APPLE`/`LINUX` conditionals and the `IO_URING_ENABLED` compile definition.

### Compile Definitions

Source files are compiled with either `SERVER` or `CLI` definition to conditionally include server-side or client-side code. This means shared headers (like `client.h`, `networking.h`) have `#ifdef SERVER`/`#ifdef CLI` blocks.

### Command System

Commands follow a split handler pattern:
- `src/commands/server/server_command_handlers.c` — server-side execution of commands (SET, GET, INCR, etc.)
- `src/commands/client/client_command_handlers.c` — client-side response parsing
- `src/commands/common/command_registry.c` — command lookup table
- `src/commands/common/command_parser.c` — client-side command parsing

### Core Data Structures

- **Hashtable** (`src/core/hashtable.c`) — hash table with collision chaining; stores two encoding types: `VALUE_ENTRY_TYPE_INT` (1) and `VALUE_ENTRY_TYPE_RAW` (2). The `value_entry_t` struct uses an `encoding` field (4-bit bitfield)
- **Linked List** (`src/core/list.c`) — doubly-linked list used for client connection management

### Key State Structures

- `server_t` in `src/server.h` — central server state: database store (`db_t *` containing `hashtable_t *store`), expire_cursor (active expiration scan position), client list, counters, config
- `client_t` in `src/client.h` — per-client state: 64KB read/write buffers, socket info, connection mode

### Networking

`src/networking/networking.c` handles socket setup, frame-based protocol processing, and supports both TCP and Unix domain sockets. Frame format is a custom binary protocol.

### Configuration

`server.conf` and `client.conf` are parsed by `src/config.c`. Config keys are simple `key value` pairs, one per line.

## Code Style

LLVM-based style with 4-space indentation, Linux brace style, no tabs. Formatting config is in `.clang-format`.

## Testing

Tests use C `assert.h` directly (no external test framework). Test sources are in `tests/`. Tests are registered with CMake's `ctest` in `CMakeLists.txt`.

## Three Executables

The project builds three separate executables from different `main()` entry points:
- `fkvs-server` — `src/server.c`
- `fkvs-cli` — `src/fkvs-cli.c` (uses linenoise for line editing)
- `fkvs-benchmark` — `src/fkvs-benchmark.c` (multi-threaded benchmark client)

## Development Guides

Detailed technical guides for implementing features, fixing bugs, and understanding the architecture are in `docs/guides/`:

- `docs/guides/adding-commands.md` — Step-by-step: 8 files to touch when adding a new command
- `docs/guides/wire-protocol.md` — Binary frame format, command layouts, response types
- `docs/guides/memory-management.md` — Pointer ownership: get_value() copies vs find_entry() direct access
- `docs/guides/testing-guide.md` — Test patterns, CMake registration, now_monotonic_ms() in tests
- `docs/guides/data-structures.md` — Hashtable, linked list, server_t (db_t contains hashtable_t *store)
- `docs/guides/event-dispatchers-deep.md` — kqueue, epoll, io_uring internals and guards
- `docs/guides/debugging-checklist.md` — Systematic checklist for code review and bug analysis
- `docs/guides/build-and-config.md` — CMake variants, source file placement, server/client config

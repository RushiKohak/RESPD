# Respd

A lightweight, Redis-compatible in-memory key-value server written in C++17, built around a single-threaded non-blocking `epoll` event loop, incremental RESP2 wire parsing with command pipelining, and an append-only command log plus periodic JSON snapshots.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Build Instructions](#build-instructions)
- [Usage](#usage)
- [Supported Commands](#supported-commands)
- [Data Persistence](#data-persistence)
- [Project Structure](#project-structure)
- [Testing](#testing)
- [Benchmarks](#benchmarks)
- [License & Attribution](#license--attribution)

## Overview

Respd is an in-memory data structure store speaking the Redis Serialization Protocol (RESP). It is built from the C++17 standard library alone; the only third-party file is `wepoll.c`, vendored so the same event loop also runs on Windows. It implements core systems engineering concepts:

- **Event-Driven I/O Multiplexing**: Single-threaded reactor loop using `epoll` on Linux and IOCP-backed `wepoll` on Windows (`server.cpp`, `net_compat.h`).
- **Non-Blocking Socket Buffers**: Per-connection `read_buffer` and `write_buffer` state with dynamic `EPOLLOUT` registration for backpressure handling.
- **Incremental Protocol Parser**: `RESP::parseOne` (`resp.cpp`) tracks byte consumption across arbitrary TCP packet boundaries and supports high-throughput client command pipelining.
- **Type-Safe Storage Engine**: Polymorphic in-memory key-value store (`db.cpp`, `value.h`) backed by `std::unordered_map` and `std::variant`.
- **Crash Recovery**: Append-Only File (`dump.aof`) command logging combined with periodic JSON snapshots (`dump.json`).

## Features

- **5 Data Types**: Strings, Integers, Lists (`std::deque`), Sets (`std::unordered_set`), and Hashes (`std::unordered_map`).
- **RESP2 & Inline Protocol**: Compatible with `redis-cli`, `redis-benchmark`, `telnet`, and `nc`.
- **Command Pipelining**: Parses and executes multiple back-to-back commands from a single `recv()` buffer and batches responses into minimal `send()` syscalls.
- **TTL & Lazy Expiration**: Per-key time-to-live (`EXPIRE`, `TTL`, `PERSIST`) using `std::chrono::steady_clock` with lazy eviction on key access.
- **Hybrid Persistence**: Write-ahead AOF logging paired with periodic and manual (`SAVE`) JSON snapshots.
- **Standalone CLI**: Includes a local interactive REPL (`main.cpp`) for direct database inspection without network I/O.

## Architecture

```text
┌────────────────────────────────────────────────────────┐
│                  Client Applications                   │
│       (redis-cli, redis-benchmark, nc, custom)         │
└───────────────────────────┬────────────────────────────┘
                            │ TCP (RESP2 / Inline)
┌───────────────────────────▼────────────────────────────┐
│              Event Loop Layer (server.cpp)             │
│  • Single-threaded epoll_wait (Linux epoll / wepoll)   │
│  • Non-blocking accept, recv, and send                 │
│  • Per-connection read_buffer and write_buffer         │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│               RESP Parser (resp.cpp)                   │
│  • Incremental parsing via RESP::parseOne              │
│  • Handles partial reads and pipelined command batches │
│  • Encodes RESP responses (+, -, :, $, *)              │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│              Database Engine (db.cpp)                  │
│  • std::unordered_map<std::string, Value>              │
│  • Lazy TTL expiration checks prior to access          │
│  • AOF logging (dump.aof) and JSON snapshots           │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│              Value Container (value.cpp)               │
│  • std::variant<string, long long, deque, set, map>    │
│  • std::optional<steady_clock::time_point> expiration  │
└────────────────────────────────────────────────────────┘

## Build Instructions

Requirements: CMake 3.16 or newer and a C++17 compiler. Nothing else — on Linux the standard
library is enough, and on Windows `wepoll.c` is compiled in place of `epoll` (MSVC or MinGW both
work; the CMake build links `ws2_32`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is `build/respd` (or `build\respd.exe` on Windows). The build defaults to Release when
no build type is given, because an unoptimised binary makes throughput meaningless.

Compiling directly, with no CMake:

```bash
g++ -std=c++17 -O2 -o build/respd main_server.cpp server.cpp db.cpp resp.cpp value.cpp -pthread
```

## Usage

```bash
./build/respd        # listens on 6379
./build/respd 7000   # or any other port
```

Any Redis client works, because the wire protocol is RESP2:

```bash
redis-cli -p 6379 SET greeting hello
redis-cli -p 6379 GET greeting
redis-cli -p 6379 LPUSH queue a b c
redis-cli -p 6379 LRANGE queue 0 -1
```

```bash
redis-benchmark -p 6379 -t set,get -n 100000 -c 50 -q
```

`main.cpp` is an older interactive REPL for inspecting the database locally, without opening a
socket. It is not part of the CMake target; the server above is.

## Supported Commands

39 commands, in `server.cpp`:

| Family | Commands |
| --- | --- |
| Connection | `PING`, `ECHO`, `QUIT` |
| Strings and keys | `SET`, `GET`, `APPEND`, `STRLEN`, `DEL`, `EXISTS`, `TYPE` |
| Counters | `INCR`, `INCRBY`, `DECR`, `DECRBY` |
| Expiry | `EXPIRE`, `TTL`, `PERSIST` |
| Lists | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`, `LINDEX`, `LSET` |
| Sets | `SADD`, `SREM`, `SMEMBERS`, `SISMEMBER`, `SCARD` |
| Hashes | `HSET`, `HGET`, `HDEL`, `HGETALL`, `HKEYS`, `HVALS`, `HLEN`, `HEXISTS` |
| Admin | `SAVE` |

Not implemented, and therefore not claimed: transactions (`MULTI`/`EXEC`), pub/sub, replication,
`CONFIG`, cluster mode and scripting. `redis-benchmark` prints a `CONFIG` warning for exactly that
reason; it is harmless.

## Data Persistence

Two files, both written next to the working directory:

- **`dump.aof`** — an append-only command log. Every write command is appended and flushed as it
  executes, and the log is replayed at startup.
- **`dump.json`** — a full snapshot of the keyspace, written on `SAVE` and periodically. A snapshot
  is taken and then the AOF is rolled over, so the log only ever holds commands newer than the
  snapshot.

Startup loads the snapshot first, then replays the AOF on top of it. The snapshot is JSON, not
Redis's binary RDB format, even though the field that holds its filename is still called
`rdb_filename_` from history. There is no `fsync`, so a hard power loss can lose the last writes
that reached the OS but not the disk; a clean `SIGINT`/`SIGTERM` shutdown saves and closes both
files.

## Project Structure

```text
main_server.cpp     entry point: parses the port, wires Db to Server
server.cpp/.h       epoll event loop, connection buffers, command dispatch
resp.cpp/.h         incremental RESP2 parser and reply encoder
db.cpp/.h           keyspace, TTL, AOF logging, JSON snapshots
value.cpp/.h        std::variant value type with an optional expiry
net_compat.h        Linux epoll / Windows wepoll socket compatibility layer
wepoll.c/.h         vendored IOCP-backed epoll shim for Windows (third party)
main.cpp            interactive REPL, not built by CMake
test_regression.py  end-to-end protocol and persistence test suite
BENCHMARKS.md       measured comparisons against Redis
DESIGN.md           architecture notes and diagrams
```

## Testing

`test_regression.py` starts the built binary on port 6399, drives it over raw TCP sockets with RESP
frames, and covers connection commands, strings, counters, lists, sets, hashes, `WRONGTYPE` errors,
TTL and lazy expiration, pipelining and AOF replay across a restart.

```bash
python3 test_regression.py ./build/respd
```

It prints `=== SUMMARY: N passed, M failed ===` and exits non-zero if anything fails. On the commit
currently benchmarked: **90 passed, 0 failed**.

## Benchmarks

See [BENCHMARKS.md](BENCHMARKS.md) for the full method and every repetition. Headline medians of
three `redis-benchmark` runs (`-n 100000 -c 50`) on one laptop, Respd against Redis 7.4.12:

| Operation | Respd | Redis 7.4.12 |
| --- | --- | --- |
| `SET`, no pipelining | 88,339 req/s | 95,877 req/s |
| `GET`, no pipelining | 87,336 req/s | 100,705 req/s |
| `SET`, `-P 16` | 476,190 req/s | 884,956 req/s |
| `GET`, `-P 16` | 1,010,101 req/s | 1,086,957 req/s |

Pipelined `SET` is the honest gap: Respd flushes every write to its AOF, while the Redis used for
comparison had AOF disabled. The other three columns are within 7–13% of Redis.

## License & Attribution

MIT — see [LICENSE](LICENSE).

`wepoll.c` / `wepoll.h` are by Bert Belder, included under their own MIT licence so the event loop
works on Windows. Everything else is written from the standard library.

# Respd

A lightweight, Redis-compatible in-memory key-value server written in C++17, built around a single-threaded non-blocking `epoll` event loop, incremental RESP2 wire parsing with command pipelining, and hybrid RDB/AOF persistence.

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
- [License & Attribution](#license--attribution)

## Overview

Respd is an in-memory data structure store speaking the Redis Serialization Protocol (RESP). Built in C++17 with zero external library dependencies, it implements core systems engineering concepts:

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
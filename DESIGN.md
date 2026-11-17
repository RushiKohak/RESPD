# Respd Architecture & Design Document

This document explains the internal architecture, engineering decisions, data flow, and trade-offs in **Respd**.

---

## 1. System Architecture

Respd uses a single-threaded, event-driven reactor architecture (`server.cpp`) backed by an in-memory polymorphic key-value store (`db.cpp`, `value.h`).

```text
  Client A ──┐                                    ┌──► RESP::parseOne (resp.cpp)
  Client B ──┼──► Non-blocking TCP Sockets        │      (Incremental framing)
  Client C ──┘           │                        │               │
                         ▼                        │               ▼
               ┌───────────────────┐              │    Server::executeCommand
               │    epoll_wait     │              │               │
               │ (Linux / wepoll)  │              │               ▼
               └─────────┬─────────┘              │    ┌─────────────────────┐
                         │                        │    │      Db Engine      │
          ┌──────────────┼──────────────┐         │    │   (bucketstore +    │
          ▼              ▼              ▼         │    │   lazy TTL check)   │
    [EPOLLIN:      [EPOLLIN:      [EPOLLOUT:      │    └──────────┬──────────┘
     Server FD]     Client FD]     Client FD]     │               │
          │              │              │         │       ┌───────┴───────┐
          ▼              ▼              ▼         │       ▼               ▼
    handleAccept()  handleRead()  handleWrite()   │   dump.aof        dump.json
                         │              ▲         │  (Write Log)     (Snapshot)
                         ▼              │         │
                  conn->read_buffer ────┴─────────┘
                  conn->write_buffer
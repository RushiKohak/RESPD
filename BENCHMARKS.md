# Benchmarks

Every number here was produced by `redis-benchmark` against this repository's code, on one laptop.
They are a comparison between two servers on the same machine — not a claim about production
throughput. A different CPU, container memory limit or host filesystem will move them.

## What was compared

| | Respd | Redis |
| --- | --- | --- |
| Version | this repo, commit `4b96062` | `redis:7-alpine` (Redis 7.4.12) |
| Build | `g++ -std=c++17 -O2` (gcc 13) | official image, default config |
| Persistence | AOF (`dump.aof`): every write command appended and flushed to the OS, plus JSON snapshots | default (RDB only, no AOF) |
| Threads | one: single-threaded `epoll` event loop | Redis's own event loop |

Both servers ran in containers on the same Docker bridge network, and `redis-benchmark` ran in a
third container on that network, so neither server was favoured by the client's location.

## Environment

- Laptop: 12th Gen Intel Core i5-12500H, 8 GB RAM.
- Docker Desktop on Windows, VM given 16 CPUs and 3.9 GiB memory.
- `redis-benchmark -t set,get -n 100000 -c 50` (100,000 requests, 50 clients).
- `-P 1` is one request per round trip; `-P 16` pipelines 16 commands per write.
- Three repetitions of every configuration; **the median of the three is reported**.
- Respd's data files were kept inside the container's filesystem, not on a bind mount, so the
  Windows filesystem was not in the write path.

## Results

Requests per second, median of three runs.

| Operation | Pipelining | Respd | Redis 7.4.12 | Respd / Redis |
| --- | --- | --- | --- | --- |
| `SET` | none (`-P 1`) | 88,339 | 95,877 | 92% |
| `GET` | none (`-P 1`) | 87,336 | 100,705 | 87% |
| `SET` | `-P 16` | 476,190 | 884,956 | 54% |
| `GET` | `-P 16` | 1,010,101 | 1,086,957 | 93% |

All three runs behind those medians:

| Server | Op | Pipe | Run 1 | Run 2 | Run 3 | Median |
| --- | --- | --- | --- | --- | --- | --- |
| Respd | `SET` | 1 | 80,321 | 88,339 | 110,865 | 88,339 |
| Respd | `GET` | 1 | 88,496 | 86,730 | 87,336 | 87,336 |
| Respd | `SET` | 16 | 476,190 | 442,478 | 507,614 | 476,190 |
| Respd | `GET` | 16 | 1,111,111 | 980,392 | 1,010,101 | 1,010,101 |
| Redis | `SET` | 1 | 95,877 | 95,147 | 107,527 | 95,877 |
| Redis | `GET` | 1 | 100,705 | 93,458 | 109,769 | 100,705 |
| Redis | `SET` | 16 | 909,091 | 598,802 | 884,956 | 884,956 |
| Redis | `GET` | 16 | 1,086,957 | 877,193 | 1,136,364 | 1,086,957 |

Latency of the same runs, `p50` as printed by `redis-benchmark`:

| Server | Op | Pipe | p50 |
| --- | --- | --- | --- |
| Respd | `SET` | 1 | 0.231 – 0.287 ms |
| Respd | `GET` | 1 | 0.271 – 0.295 ms |
| Respd | `SET` | 16 | 1.503 – 1.599 ms per batch |
| Respd | `GET` | 16 | 0.375 – 0.399 ms per batch |
| Redis | `SET` | 1 | 0.231 – 0.263 ms |
| Redis | `GET` | 1 | 0.231 – 0.263 ms |
| Redis | `SET` | 16 | 0.719 – 0.759 ms per batch |
| Redis | `GET` | 16 | 0.551 – 0.615 ms per batch |

## Reading these numbers

- **Unpipelined throughput is within about 8–13% of Redis.** The request path here is one `epoll`
  wait, one `recv`, a RESP parse, an `unordered_map` operation and one `send`; at 50 clients the
  cost is dominated by that round trip, and a single thread is enough to saturate it.
- **Pipelined `GET` keeps pace (93%).** Parsing many commands from one buffer and batching the
  replies is exactly what pipelining rewards, and read-only commands do no persistence work.
- **Pipelined `SET` is the clear gap (54%).** Respd appends every write to `dump.aof`, while stock
  Redis in this comparison had AOF off entirely. The `SET` columns are therefore not like for like:
  Respd is paying for durability on each write that Redis was not. Turning AOF off (the code has no
  such switch yet) would measure the storage engine alone; adding one is the honest next experiment.
- **The variance is large** (for example Redis `SET -P 16` spans 599k–909k across three runs on a
  shared laptop), which is why the median of three is reported rather than a best run.

## Reproducing

```bash
# build
docker run --rm -v "$PWD:/src" -w /src gcc:13 \
  bash -c "g++ -std=c++17 -O2 -o build/respd main_server.cpp server.cpp db.cpp resp.cpp value.cpp -pthread"

# both servers on one network
docker network create respd-bench
docker run -d --name respd-srv --network respd-bench -v "$PWD:/src" gcc:13 \
  bash -c "cp /src/build/respd /tmp/respd && exec /tmp/respd 6379"
docker run -d --name redis-ref --network respd-bench redis:7-alpine

# measure
for pipe in 1 16; do
  docker run --rm --network respd-bench redis:7-alpine \
    redis-benchmark -h respd-srv -t set,get -n 100000 -c 50 -P $pipe -q
  docker run --rm --network respd-bench redis:7-alpine \
    redis-benchmark -h redis-ref -t set,get -n 100000 -c 50 -P $pipe -q
done
```

`redis-benchmark` prints `WARNING: Could not fetch server CONFIG`, because `CONFIG` is not
implemented here. It is a warning, not a failure: the benchmark proceeds normally.

## Correctness alongside the numbers

Speed says nothing about whether a server is right. `test_regression.py` drives the same build over
raw TCP sockets across connection, string, integer, list, set, hash, TTL, pipelining and
persistence-after-restart cases:

```bash
docker run --rm -v "$PWD:/src" -w /src python:3.12-slim python3 test_regression.py ./build/respd
```

Result on the commit measured above: **90 passed, 0 failed**.

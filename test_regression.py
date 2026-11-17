#!/usr/bin/env python3
"""
Regression test suite for Respd (Step 2).
Tests all active command families in server.cpp over raw TCP sockets using RESP:
- Connection & Server (PING, ECHO, QUIT)
- Strings (SET, GET, APPEND, STRLEN, DEL, EXISTS, TYPE)
- Integers (INCR, INCRBY, DECR, DECRBY)
- Lists (LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE, LINDEX, LSET)
- Sets (SADD, SREM, SMEMBERS, SISMEMBER, SCARD)
- Hashes (HSET, HGET, HDEL, HGETALL, HKEYS, HVALS, HLEN, HEXISTS)
- Key Expiry / TTL (EXPIRE, TTL, PERSIST, lazy expiration)
- Error handling (WRONGTYPE, bad integer args, unknown commands)
- Persistence & Recovery (AOF replay across server restart + SAVE snapshot)
"""

import os
import socket
import subprocess
import sys
import time

PORT = 6399
HOST = "127.0.0.1"


def encode_resp(*args):
    """Encode a command tuple into a RESP array of bulk strings."""
    out = [f"*{len(args)}\r\n".encode()]
    for arg in args:
        s = str(arg).encode()
        out.append(f"${len(s)}\r\n".encode() + s + b"\r\n")
    return b"".join(out)


class RespReader:
    """Minimal buffered RESP response reader over a TCP socket."""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def _read_line(self):
        while b"\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("Socket closed while reading line")
            self.buf += chunk
        idx = self.buf.index(b"\r\n")
        line = self.buf[:idx]
        self.buf = self.buf[idx + 2 :]
        return line

    def _read_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("Socket closed while reading bulk string")
            self.buf += chunk
        data = self.buf[:n]
        self.buf = self.buf[n:]
        return data

    def read_reply(self):
        line = self._read_line()
        prefix = chr(line[0])
        payload = line[1:].decode("utf-8", errors="replace")

        if prefix == "+":  # Simple String
            return ("simple", payload)
        elif prefix == "-":  # Error
            return ("error", payload)
        elif prefix == ":":  # Integer
            return ("int", int(payload))
        elif prefix == "$":  # Bulk String
            length = int(payload)
            if length == -1:
                return ("nil", None)
            data = self._read_exact(length)
            crlf = self._read_exact(2)
            assert crlf == b"\r\n", f"Expected CRLF after bulk string, got {crlf}"
            return ("bulk", data.decode("utf-8", errors="replace"))
        elif prefix == "*":  # Array
            count = int(payload)
            if count == -1:
                return ("nil_array", None)
            items = []
            for _ in range(count):
                kind, val = self.read_reply()
                items.append(val)
            return ("array", items)
        else:
            raise ValueError(f"Unknown RESP prefix: {line!r}")


class Client:
    def __init__(self, host=HOST, port=PORT):
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.reader = RespReader(self.sock)

    def cmd(self, *args):
        self.sock.sendall(encode_resp(*args))
        return self.reader.read_reply()

    def inline(self, raw_line):
        self.sock.sendall((raw_line + "\r\n").encode())
        return self.reader.read_reply()

    def close(self):
        self.sock.close()


def cleanup_dump_files():
    for fname in ("dump.json", "dump.aof"):
        if os.path.exists(fname):
            os.remove(fname)


def start_server(binary_path):
    proc = subprocess.Popen(
        [binary_path, str(PORT)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    # Wait until server accepts connections
    deadline = time.time() + 5.0
    while time.time() < deadline:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.2):
                return proc
        except OSError:
            time.sleep(0.05)
    proc.kill()
    raise RuntimeError("Server failed to start on port " + str(PORT))


def stop_server(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()


def run_tests(binary_path):
    cleanup_dump_files()
    proc = start_server(binary_path)
    passed = 0
    failed = 0

    def check(name, actual, expected):
        nonlocal passed, failed
        if actual == expected:
            passed += 1
        else:
            failed += 1
            print(f"  [FAIL] {name}: expected {expected!r}, got {actual!r}")

    def check_err(name, reply, substring=""):
        nonlocal passed, failed
        if reply[0] == "error" and substring in reply[1]:
            passed += 1
        else:
            failed += 1
            print(f"  [FAIL] {name}: expected error containing {substring!r}, got {reply!r}")

    try:
        c = Client()

        print("1. Testing Connection & Basic Commands (PING, ECHO, inline)...")
        check("PING", c.cmd("PING"), ("simple", "PONG"))
        check("PING arg", c.cmd("PING", "hello"), ("bulk", "hello"))
        check("ECHO", c.cmd("ECHO", "respd"), ("bulk", "respd"))
        check("Inline PING", c.inline("PING"), ("simple", "PONG"))
        check_err("Unknown command", c.cmd("FOOBAR"), "unknown command")

        print("2. Testing String & Key Commands (SET, GET, APPEND, STRLEN, DEL, EXISTS, TYPE)...")
        check("SET k1", c.cmd("SET", "k1", "hello"), ("simple", "OK"))
        check("GET k1", c.cmd("GET", "k1"), ("bulk", "hello"))
        check("GET missing", c.cmd("GET", "nope"), ("nil", None))
        check("APPEND existing", c.cmd("APPEND", "k1", "world"), ("int", 10))
        check("GET after APPEND", c.cmd("GET", "k1"), ("bulk", "helloworld"))
        check("STRLEN k1", c.cmd("STRLEN", "k1"), ("int", 10))
        check("STRLEN missing", c.cmd("STRLEN", "nope"), ("int", 0))
        check("APPEND new key", c.cmd("APPEND", "k2", "abc"), ("int", 3))
        check("EXISTS k1 k2 nope", c.cmd("EXISTS", "k1", "k2", "nope"), ("int", 2))
        check("TYPE string", c.cmd("TYPE", "k1"), ("simple", "string"))
        check("TYPE missing", c.cmd("TYPE", "nope"), ("simple", "none"))
        check("DEL k1 k2 nope", c.cmd("DEL", "k1", "k2", "nope"), ("int", 2))
        check("EXISTS after DEL", c.cmd("EXISTS", "k1"), ("int", 0))

        print("3. Testing Integer Commands (INCR, INCRBY, DECR, DECRBY)...")
        check("INCR new", c.cmd("INCR", "cnt"), ("int", 1))
        check("INCR existing", c.cmd("INCR", "cnt"), ("int", 2))
        check("INCRBY", c.cmd("INCRBY", "cnt", "10"), ("int", 12))
        check("DECR", c.cmd("DECR", "cnt"), ("int", 11))
        check("DECRBY", c.cmd("DECRBY", "cnt", "5"), ("int", 6))
        check("GET integer key", c.cmd("GET", "cnt"), ("bulk", "6"))
        check("TYPE integer key", c.cmd("TYPE", "cnt"), ("simple", "string"))
        check_err("INCRBY bad int", c.cmd("INCRBY", "cnt", "notanumber"), "not an integer")

        print("4. Testing List Commands (LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE, LINDEX, LSET)...")
        check("LPUSH", c.cmd("LPUSH", "mylist", "b", "a"), ("int", 2))
        check("RPUSH", c.cmd("RPUSH", "mylist", "c", "d"), ("int", 4))
        check("TYPE list", c.cmd("TYPE", "mylist"), ("simple", "list"))
        check("LLEN", c.cmd("LLEN", "mylist"), ("int", 4))
        # Note: LPUSH mylist b a pushes 'a' then 'b' to front via reverse iterator -> ['b', 'a', 'c', 'd']
        check("LRANGE all", c.cmd("LRANGE", "mylist", "0", "-1"), ("array", ["b", "a", "c", "d"]))
        check("LINDEX 1", c.cmd("LINDEX", "mylist", "1"), ("bulk", "a"))
        check("LINDEX -1", c.cmd("LINDEX", "mylist", "-1"), ("bulk", "d"))
        check("LINDEX OOB", c.cmd("LINDEX", "mylist", "99"), ("nil", None))
        check("LSET 1", c.cmd("LSET", "mylist", "1", "A"), ("simple", "OK"))
        check("LINDEX after LSET", c.cmd("LINDEX", "mylist", "1"), ("bulk", "A"))
        check_err("LSET OOB", c.cmd("LSET", "mylist", "99", "X"), "index out of range")
        check_err("LSET no key", c.cmd("LSET", "nolist", "0", "X"), "no such key")
        check("LPOP", c.cmd("LPOP", "mylist"), ("bulk", "b"))
        check("RPOP", c.cmd("RPOP", "mylist"), ("bulk", "d"))
        check("LLEN after pop", c.cmd("LLEN", "mylist"), ("int", 2))

        print("5. Testing Set Commands (SADD, SREM, SMEMBERS, SISMEMBER, SCARD)...")
        check("SADD", c.cmd("SADD", "myset", "x", "y", "z", "x"), ("int", 3))
        check("TYPE set", c.cmd("TYPE", "myset"), ("simple", "set"))
        check("SCARD", c.cmd("SCARD", "myset"), ("int", 3))
        check("SISMEMBER hit", c.cmd("SISMEMBER", "myset", "y"), ("int", 1))
        check("SISMEMBER miss", c.cmd("SISMEMBER", "myset", "w"), ("int", 0))
        kind, members = c.cmd("SMEMBERS", "myset")
        check("SMEMBERS", (kind, sorted(members)), ("array", ["x", "y", "z"]))
        check("SREM hit", c.cmd("SREM", "myset", "y"), ("int", 1))
        check("SREM miss", c.cmd("SREM", "myset", "y"), ("int", 0))
        check("SCARD after SREM", c.cmd("SCARD", "myset"), ("int", 2))

        print("6. Testing Hash Commands (HSET, HGET, HDEL, HGETALL, HKEYS, HVALS, HLEN, HEXISTS)...")
        check("HSET new field", c.cmd("HSET", "myhash", "f1", "v1"), ("int", 1))
        check("HSET update field", c.cmd("HSET", "myhash", "f1", "v1_new"), ("int", 0))
        check("HSET f2", c.cmd("HSET", "myhash", "f2", "v2"), ("int", 1))
        check("TYPE hash", c.cmd("TYPE", "myhash"), ("simple", "hash"))
        check("HGET f1", c.cmd("HGET", "myhash", "f1"), ("bulk", "v1_new"))
        check("HGET missing", c.cmd("HGET", "myhash", "f99"), ("nil", None))
        check("HLEN", c.cmd("HLEN", "myhash"), ("int", 2))
        check("HEXISTS hit", c.cmd("HEXISTS", "myhash", "f2"), ("int", 1))
        check("HEXISTS miss", c.cmd("HEXISTS", "myhash", "f99"), ("int", 0))
        _, hkeys = c.cmd("HKEYS", "myhash")
        check("HKEYS", sorted(hkeys), ["f1", "f2"])
        _, hvals = c.cmd("HVALS", "myhash")
        check("HVALS", sorted(hvals), ["v1_new", "v2"])
        _, hgetall = c.cmd("HGETALL", "myhash")
        pairs = dict(zip(hgetall[0::2], hgetall[1::2]))
        check("HGETALL", pairs, {"f1": "v1_new", "f2": "v2"})
        check("HDEL hit", c.cmd("HDEL", "myhash", "f1"), ("int", 1))
        check("HDEL miss", c.cmd("HDEL", "myhash", "f1"), ("int", 0))
        check("HLEN after HDEL", c.cmd("HLEN", "myhash"), ("int", 1))

        print("7. Testing Type Mismatches (WRONGTYPE)...")
        check_err("GET on list", c.cmd("GET", "mylist"), "WRONGTYPE")
        check_err("INCR on list", c.cmd("INCR", "mylist"), "WRONGTYPE")
        check_err("LPUSH on set", c.cmd("LPUSH", "myset", "a"), "WRONGTYPE")
        check_err("SADD on hash", c.cmd("SADD", "myhash", "a"), "WRONGTYPE")
        check_err("HSET on list", c.cmd("HSET", "mylist", "k", "v"), "WRONGTYPE")

        print("8. Testing TTL & Lazy Expiration (EXPIRE, TTL, PERSIST)...")
        check("SET ttlkey", c.cmd("SET", "ttlkey", "temp"), ("simple", "OK"))
        check("TTL no expiry", c.cmd("TTL", "ttlkey"), ("int", -1))
        check("TTL missing key", c.cmd("TTL", "no_ttl_key"), ("int", -2))
        check("EXPIRE 10s", c.cmd("EXPIRE", "ttlkey", "10"), ("int", 1))
        _, rem_ttl = c.cmd("TTL", "ttlkey")
        check("TTL > 0", 1 <= rem_ttl <= 10, True)
        check("PERSIST", c.cmd("PERSIST", "ttlkey"), ("int", 1))
        check("TTL after PERSIST", c.cmd("TTL", "ttlkey"), ("int", -1))
        check("EXPIRE 1s", c.cmd("EXPIRE", "ttlkey", "1"), ("int", 1))
        time.sleep(1.2)
        check("GET expired key", c.cmd("GET", "ttlkey"), ("nil", None))
        check("TTL expired key", c.cmd("TTL", "ttlkey"), ("int", -2))

        print("9. Testing Pipelining & QUIT...")
        pipeline_payload = (
            encode_resp("SET", "p1", "val1")
            + encode_resp("INCR", "pcnt")
            + encode_resp("GET", "p1")
        )
        c.sock.sendall(pipeline_payload)
        check("Pipeline 1", c.reader.read_reply(), ("simple", "OK"))
        check("Pipeline 2", c.reader.read_reply(), ("int", 1))
        check("Pipeline 3", c.reader.read_reply(), ("bulk", "val1"))
        check("QUIT", c.cmd("QUIT"), ("simple", "OK"))
        c.close()

        print("10. Testing AOF Persistence Across Server Restart...")
        stop_server(proc)
        proc = start_server(binary_path)
        c2 = Client()
        check("AOF replay string", c2.cmd("GET", "p1"), ("bulk", "val1"))
        check("AOF replay int", c2.cmd("GET", "cnt"), ("bulk", "6"))
        check("AOF replay list", c2.cmd("LRANGE", "mylist", "0", "-1"), ("array", ["A", "c"]))
        check("AOF replay set", c2.cmd("SISMEMBER", "myset", "x"), ("int", 1))
        check("AOF replay hash", c2.cmd("HGET", "myhash", "f2"), ("bulk", "v2"))
        check("SAVE command", c2.cmd("SAVE"), ("simple", "OK"))
        c2.close()

    finally:
        stop_server(proc)
        cleanup_dump_files()

    print(f"\n=== SUMMARY: {passed} passed, {failed} failed ===")
    return failed == 0


if __name__ == "__main__":
    binary = sys.argv[1] if len(sys.argv) > 1 else "./respd"
    if not os.path.exists(binary):
        print(f"Error: server binary '{binary}' not found. Compile first or pass path as argv[1].")
        sys.exit(1)
    success = run_tests(binary)
    sys.exit(0 if success else 1)
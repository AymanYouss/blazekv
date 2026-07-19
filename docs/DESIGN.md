# BlazeKV design notes

This document explains how BlazeKV is put together and, importantly, the
trade-offs behind the thread-per-core design.

## 1. Execution model: thread-per-core, shared-nothing

BlazeKV runs `N` **shards**, one per core, each pinned with `sched_setaffinity`.
A shard owns:

- a hash-partition of the keyspace (`Keyspace`: a Swiss table of `key -> Object`
  plus a parallel `key -> deadline` table for TTLs),
- its own event loop (`Reactor`),
- its own AOF and snapshot files,
- its own metrics counters and latency histogram,
- its own slab allocator.

There is **no global data structure guarded by a lock**. The server object only
coordinates start-up, shutdown, and snapshot orchestration.

### Connection distribution

Each shard opens its own listening socket with `SO_REUSEPORT`. The kernel
load-balances incoming connections across those sockets, so connections are
spread across cores without a user-space accept lock or a dispatcher thread.

### Command routing

For every parsed command we compute the owning shard of each key
(`hash_bytes(key) % N`, a wyhash-based function that is identical on every core):

- **All keys local** → execute inline on the connection's shard. This is the
  fast path: no atomics, no messaging, just a Swiss-table probe and a reply
  appended to the output buffer.
- **Single remote owner** → forward the whole command to that shard over its
  mailbox.
- **Multi-key spanning shards** (`MGET`/`MSET`/`DEL`/`UNLINK`/`EXISTS`/`TOUCH`)
  → decompose into single-key sub-requests, fan out to each owner, and reassemble
  in key order. Other multi-key commands that would span shards (e.g. `RENAME`)
  return a `CROSSSLOT` error, mirroring Redis Cluster.

### The mailbox

Cross-shard messages travel through a **lock-free MPSC queue** (a Treiber-style
intrusive stack drained and reversed to restore FIFO order — wait-free for
producers, lock-free for the single consumer). A shard is woken via an
`eventfd` (Linux) or self-pipe. Because single-key commands never touch the
mailbox, the shared-nothing hot path stays free of contention.

### Pipelining across shards

A naive design would block a connection until its forwarded command returns,
serializing the whole client pipeline behind cross-thread round-trips. BlazeKV
instead keeps an **ordered reply pipeline** per connection: each command is
assigned a submission sequence and dispatched immediately; remote commands run
**concurrently across shards** while later pipelined commands keep flowing;
completions fill their slot, and replies are flushed to the socket strictly in
submission order. This keeps pipelining correct without stalling on hops.

## 2. The cross-shard trade-off (read this)

Shared-nothing sharding is a genuine trade-off, and it is worth being explicit.

On a **uniform-random single-node** workload (what `redis-benchmark` generates),
roughly `1 - 1/N` of commands target a *remote* shard and pay a cross-thread hop.
Redis, being single-threaded with no hops, is close to optimal for exactly this
workload. Measured single-threaded head-to-head, BlazeKV **matches** Redis; with
many shards on uniform-random keys, the hop cost bounds throughput.

The design pays off when:

- **access is shard-local** (client-side sharding / hash-tag affinity, like a
  Redis Cluster-aware client), so most commands execute inline on many cores in
  parallel; or
- you want **per-core isolation** — a slow command on one shard cannot stall the
  others — and **independent per-shard persistence**.

This is the same trade-off ScyllaDB and Dragonfly make; BlazeKV implements the
mechanism (sharding, lock-free messaging, pipelined fan-out) end-to-end.

## 3. Data structures

### Swiss table (`swiss_table.hpp`)
Open-addressing hash table with a control byte per slot holding the low 7 bits of
the hash. A full 16-slot group is probed in parallel with one SIMD compare
(`_mm_cmpeq_epi8` on x86, a `vshrn` movemask trick on NEON), which keeps GET/SET
branchless and cache-friendly. Deletion uses tombstones with a same-size rehash
to reclaim them, so probe chains never break and probing always terminates.

### Skip list (`skiplist.hpp`)
Indexable skip list ordered by `(score, member)` with per-level spans, giving
`O(log N)` `ZADD`/`ZRANGE`/`ZRANK` alongside the `O(1)` `member -> score`
dictionary that backs the sorted set.

### Slab allocator (`arena.hpp`)
Per-shard segregated free-lists carved from 1 MiB slabs for small objects, with a
bump `BumpArena` for lifetime-batched allocations. Being shard-local, allocation
is a thread-local pop with no synchronization.

### HNSW (`hnsw.hpp`)
Hierarchical Navigable Small World graph (Malkov & Yashunin) for approximate
nearest-neighbor search over embeddings, exposed as `VADD`/`VSIM`. Cosine and L2
metrics; deletes are tombstoned for `O(1)` removal.

### HyperLogLog (`hyperloglog.hpp`)
Dense HLL with 2^14 registers (~0.81% standard error), stored inside an ordinary
string value so it snapshots and replays like any other key.

## 4. Networking

`Reactor` is an interface with three backends selected at runtime:

- **io_uring** — multishot `POLL_ADD` so one submission keeps delivering
  readiness; the fast path on modern Linux.
- **epoll** — level-triggered fallback.
- **poll** — portable baseline for macOS and constrained CI.

The connection loop reads into a buffer, parses as many complete commands as are
available, dispatches them, and coalesces replies into a single `writev`-friendly
output buffer.

## 5. Persistence

- **AOF** — write commands are appended as RESP; fsync cadence is `always`,
  `everysec`, or `no`. Each shard writes its own `*.aof.<id>`.
- **Snapshot** — the server `fork()`s; the child serializes each shard's keyspace
  from its copy-on-write memory image while the parent keeps serving, then exits.
  This is the RDB `bgsave` technique applied per shard.
- **Recovery** — on start each shard loads its snapshot then replays its AOF.

## 6. Observability

Each shard maintains atomic counters (commands, hits/misses, cross-shard ops,
memory, keys) and a log-scale latency histogram. The server renders these as
Prometheus text at `/metrics`, as an `INFO` payload, and as JSON at `/api/stats`
for the dashboard. A `/api/query` endpoint runs a command via a loopback client
so the web console can execute arbitrary commands.

## 7. What is intentionally out of scope

Replication and Raft-based clustering are on the roadmap but not implemented; the
sharding, messaging, and persistence layers are built to accommodate them. Some
commands are a deliberately reduced subset of Redis (e.g. no blocking list ops,
no scripting).

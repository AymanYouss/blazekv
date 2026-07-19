#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "blazekv/config.hpp"
#include "blazekv/keyspace.hpp"

namespace blazekv {

// Append-only log for a single shard. Write commands are encoded as RESP and
// appended here; fsync cadence is governed by FsyncPolicy (Always for durability,
// EverySec for the usual latency/durability trade-off, No to defer to the OS).
class Aof {
   public:
    ~Aof() { close(); }

    bool open(const std::string& path, FsyncPolicy policy, std::string& err);
    void append(std::string_view encoded);
    // Invoked once per second by the shard loop; performs the EverySec fsync.
    void tick();
    void flush();
    void close();
    bool enabled() const { return fd_ >= 0; }
    std::uint64_t bytes_written() const { return bytes_written_; }
    // Rewrites the AOF from a base state to compact it (called after snapshot).
    void reset_file(const std::string& path, std::string& err);

   private:
    void do_fsync();
    int fd_ = -1;
    std::string path_;
    FsyncPolicy policy_ = FsyncPolicy::EverySec;
    std::string buf_;
    std::uint64_t bytes_written_ = 0;
    std::uint64_t last_fsync_ms_ = 0;
    bool dirty_since_fsync_ = false;
};

// Point-in-time snapshot serialization for one shard's keyspace. The server
// orchestrates a fork() so serialization runs against a copy-on-write image while
// the shard keeps serving; these functions do the actual (de)serialization.
namespace snapshot {

// Writes `ks` to `path` atomically (temp file + rename). Safe to call from a
// forked child that shares the parent's COW memory.
bool save(const std::string& path, Keyspace& ks, std::string& err);

// Loads a snapshot file into `ks`, honoring stored TTLs (dropping already-expired
// keys). Missing file is treated as an empty, successful load.
bool load(const std::string& path, Keyspace& ks, std::string& err);

}  // namespace snapshot
}  // namespace blazekv

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "blazekv/keyspace.hpp"
#include "blazekv/resp.hpp"

namespace blazekv {

class Shard;

// Everything a command handler needs. The handler reads `cmd`, mutates `db`, and
// writes its reply through `reply`. Setting `dirty` marks the command as a write
// that must be appended to the AOF / propagated to replicas.
struct CommandContext {
    const Command& cmd;
    ReplyBuilder& reply;
    Keyspace& db;
    Shard& shard;
    bool dirty = false;
    int hits = 0;    // keyspace lookups that found a live key
    int misses = 0;  // keyspace lookups that found nothing

    std::size_t argc() const noexcept { return cmd.argv.size(); }
    std::string_view arg(std::size_t i) const noexcept { return cmd.argv[i]; }

    void mark_dirty() noexcept { dirty = true; }
    void hit() noexcept { ++hits; }
    void miss() noexcept { ++misses; }

    // Common typed error replies (kept identical to Redis wording so clients and
    // their assertions behave unchanged).
    void err_wrong_args();
    void err_wrong_type();
    void err_not_int();
    void err_not_float();
    void err_syntax();
    void err(std::string_view msg) { reply.error(msg); }
};

using CommandFn = void (*)(CommandContext&);

struct CommandSpec {
    std::string_view name;
    CommandFn fn;
    int arity;      // >=0 exact arg count; <0 minimum (-N means at least N)
    bool write;     // participates in persistence/replication
    int first_key;  // 1-based index of first key arg (0 = no keys)
    int last_key;   // negative counts from end
    int key_step;   // stride between keys
};

// Immutable global command table, built once at startup.
class CommandTable {
   public:
    static const CommandTable& instance();
    const CommandSpec* find(std::string_view name) const;
    const std::vector<CommandSpec>& all() const { return specs_; }

   private:
    CommandTable();
    std::vector<CommandSpec> specs_;
    std::unordered_map<std::string, const CommandSpec*> by_name_;
};

// Extract the keys a command touches, for shard routing. Lowercase name assumed.
std::vector<std::string_view> extract_keys(const CommandSpec& spec, const Command& cmd);

// ---- handler declarations (defined per-type in commands/*.cpp) ----
namespace cmd {
// strings
void get(CommandContext&);
void set(CommandContext&);
void setnx(CommandContext&);
void setex(CommandContext&);
void getset(CommandContext&);
void append(CommandContext&);
void strlen_(CommandContext&);
void incr(CommandContext&);
void decr(CommandContext&);
void incrby(CommandContext&);
void decrby(CommandContext&);
void incrbyfloat(CommandContext&);
void mget(CommandContext&);
void mset(CommandContext&);
void getrange(CommandContext&);
void setrange(CommandContext&);

// generic / keyspace
void del(CommandContext&);
void unlink(CommandContext&);
void exists(CommandContext&);
void expire(CommandContext&);
void pexpire(CommandContext&);
void expireat(CommandContext&);
void ttl(CommandContext&);
void pttl(CommandContext&);
void persist(CommandContext&);
void type(CommandContext&);
void keys(CommandContext&);
void scan(CommandContext&);
void rename(CommandContext&);
void randomkey(CommandContext&);
void object_(CommandContext&);

// hashes
void hset(CommandContext&);
void hget(CommandContext&);
void hdel(CommandContext&);
void hexists(CommandContext&);
void hgetall(CommandContext&);
void hkeys(CommandContext&);
void hvals(CommandContext&);
void hlen(CommandContext&);
void hincrby(CommandContext&);
void hmget(CommandContext&);

// lists
void lpush(CommandContext&);
void rpush(CommandContext&);
void lpop(CommandContext&);
void rpop(CommandContext&);
void llen(CommandContext&);
void lrange(CommandContext&);
void lindex(CommandContext&);
void lset(CommandContext&);

// sets
void sadd(CommandContext&);
void srem(CommandContext&);
void sismember(CommandContext&);
void scard(CommandContext&);
void smembers(CommandContext&);
void spop(CommandContext&);

// sorted sets
void zadd(CommandContext&);
void zrem(CommandContext&);
void zscore(CommandContext&);
void zcard(CommandContext&);
void zrank(CommandContext&);
void zrange(CommandContext&);
void zrangebyscore(CommandContext&);
void zincrby(CommandContext&);

// hyperloglog
void pfadd(CommandContext&);
void pfcount(CommandContext&);
void pfmerge(CommandContext&);

// vector (HNSW)
void vadd(CommandContext&);
void vsim(CommandContext&);
void vcard(CommandContext&);
void vrem(CommandContext&);

// server / connection
void ping(CommandContext&);
void echo(CommandContext&);
void select(CommandContext&);
void command_(CommandContext&);
void info(CommandContext&);
void dbsize(CommandContext&);
void flushall(CommandContext&);
void flushdb(CommandContext&);
void config(CommandContext&);
void hello(CommandContext&);
void client(CommandContext&);
void reset(CommandContext&);
void debug(CommandContext&);
void wait(CommandContext&);
}  // namespace cmd

}  // namespace blazekv

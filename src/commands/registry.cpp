#include "blazekv/command.hpp"

#include <string>

namespace blazekv {

// ---------------------------------------------------------------------------
// CommandContext typed error replies (wording kept identical to Redis).
// ---------------------------------------------------------------------------
void CommandContext::err_wrong_args() {
    reply.error(std::string("ERR wrong number of arguments for '")
                    .append(cmd.name())
                    .append("' command"));
}
void CommandContext::err_wrong_type() {
    reply.error("WRONGTYPE Operation against a key holding the wrong kind of value");
}
void CommandContext::err_not_int() { reply.error("ERR value is not an integer or out of range"); }
void CommandContext::err_not_float() { reply.error("ERR value is not a valid float"); }
void CommandContext::err_syntax() { reply.error("ERR syntax error"); }

// ---------------------------------------------------------------------------
// Command table
// ---------------------------------------------------------------------------
CommandTable::CommandTable() {
    using namespace cmd;
    // name, fn, arity, write, first_key, last_key, key_step
    specs_ = {
        // strings
        {"get", get, 2, false, 1, 1, 1},
        {"set", set, -3, true, 1, 1, 1},
        {"setnx", setnx, 3, true, 1, 1, 1},
        {"setex", setex, 4, true, 1, 1, 1},
        {"psetex", setex, 4, true, 1, 1, 1},
        {"getset", getset, 3, true, 1, 1, 1},
        {"append", append, 3, true, 1, 1, 1},
        {"strlen", strlen_, 2, false, 1, 1, 1},
        {"incr", incr, 2, true, 1, 1, 1},
        {"decr", decr, 2, true, 1, 1, 1},
        {"incrby", incrby, 3, true, 1, 1, 1},
        {"decrby", decrby, 3, true, 1, 1, 1},
        {"incrbyfloat", incrbyfloat, 3, true, 1, 1, 1},
        {"mget", mget, -2, false, 1, -1, 1},
        {"mset", mset, -3, true, 1, -1, 2},
        {"getrange", getrange, 4, false, 1, 1, 1},
        {"setrange", setrange, 4, true, 1, 1, 1},
        // generic
        {"del", del, -2, true, 1, -1, 1},
        {"unlink", unlink, -2, true, 1, -1, 1},
        {"exists", exists, -2, false, 1, -1, 1},
        {"touch", exists, -2, false, 1, -1, 1},
        {"expire", expire, 3, true, 1, 1, 1},
        {"pexpire", pexpire, 3, true, 1, 1, 1},
        {"expireat", expireat, 3, true, 1, 1, 1},
        {"ttl", ttl, 2, false, 1, 1, 1},
        {"pttl", pttl, 2, false, 1, 1, 1},
        {"persist", persist, 2, true, 1, 1, 1},
        {"type", type, 2, false, 1, 1, 1},
        {"keys", keys, 2, false, 0, 0, 0},
        {"scan", scan, -2, false, 0, 0, 0},
        {"rename", rename, 3, true, 1, 2, 1},
        {"randomkey", randomkey, 1, false, 0, 0, 0},
        {"object", object_, -2, false, 2, 2, 1},
        // hashes
        {"hset", hset, -4, true, 1, 1, 1},
        {"hmset", hset, -4, true, 1, 1, 1},
        {"hget", hget, 3, false, 1, 1, 1},
        {"hdel", hdel, -3, true, 1, 1, 1},
        {"hexists", hexists, 3, false, 1, 1, 1},
        {"hgetall", hgetall, 2, false, 1, 1, 1},
        {"hkeys", hkeys, 2, false, 1, 1, 1},
        {"hvals", hvals, 2, false, 1, 1, 1},
        {"hlen", hlen, 2, false, 1, 1, 1},
        {"hincrby", hincrby, 4, true, 1, 1, 1},
        {"hmget", hmget, -3, false, 1, 1, 1},
        // lists
        {"lpush", lpush, -3, true, 1, 1, 1},
        {"rpush", rpush, -3, true, 1, 1, 1},
        {"lpop", lpop, -2, true, 1, 1, 1},
        {"rpop", rpop, -2, true, 1, 1, 1},
        {"llen", llen, 2, false, 1, 1, 1},
        {"lrange", lrange, 4, false, 1, 1, 1},
        {"lindex", lindex, 3, false, 1, 1, 1},
        {"lset", lset, 4, true, 1, 1, 1},
        // sets
        {"sadd", sadd, -3, true, 1, 1, 1},
        {"srem", srem, -3, true, 1, 1, 1},
        {"sismember", sismember, 3, false, 1, 1, 1},
        {"scard", scard, 2, false, 1, 1, 1},
        {"smembers", smembers, 2, false, 1, 1, 1},
        {"spop", spop, -2, true, 1, 1, 1},
        // sorted sets
        {"zadd", zadd, -4, true, 1, 1, 1},
        {"zrem", zrem, -3, true, 1, 1, 1},
        {"zscore", zscore, 3, false, 1, 1, 1},
        {"zcard", zcard, 2, false, 1, 1, 1},
        {"zrank", zrank, 3, false, 1, 1, 1},
        {"zrange", zrange, -4, false, 1, 1, 1},
        {"zrangebyscore", zrangebyscore, -4, false, 1, 1, 1},
        {"zincrby", zincrby, 4, true, 1, 1, 1},
        // hyperloglog
        {"pfadd", pfadd, -2, true, 1, 1, 1},
        {"pfcount", pfcount, -2, false, 1, -1, 1},
        {"pfmerge", pfmerge, -2, true, 1, -1, 1},
        // vector / HNSW
        {"vadd", vadd, -4, true, 1, 1, 1},
        {"vsim", vsim, -4, false, 1, 1, 1},
        {"vcard", vcard, 2, false, 1, 1, 1},
        {"vrem", vrem, 3, true, 1, 1, 1},
        // server / connection
        {"ping", ping, -1, false, 0, 0, 0},
        {"echo", echo, 2, false, 0, 0, 0},
        {"select", select, 2, false, 0, 0, 0},
        {"command", command_, -1, false, 0, 0, 0},
        {"info", info, -1, false, 0, 0, 0},
        {"dbsize", dbsize, 1, false, 0, 0, 0},
        {"flushall", flushall, -1, false, 0, 0, 0},
        {"flushdb", flushdb, -1, false, 0, 0, 0},
        {"config", config, -2, false, 0, 0, 0},
        {"hello", hello, -1, false, 0, 0, 0},
        {"client", client, -2, false, 0, 0, 0},
        {"reset", reset, 1, false, 0, 0, 0},
        {"debug", debug, -2, false, 0, 0, 0},
        {"wait", wait, 3, false, 0, 0, 0},
    };
    by_name_.reserve(specs_.size() * 2);
    for (const auto& s : specs_) by_name_.emplace(std::string(s.name), &s);
}

const CommandTable& CommandTable::instance() {
    static const CommandTable table;
    return table;
}

const CommandSpec* CommandTable::find(std::string_view name) const {
    auto it = by_name_.find(std::string(name));
    return it == by_name_.end() ? nullptr : it->second;
}

std::vector<std::string_view> extract_keys(const CommandSpec& spec, const Command& cmd) {
    std::vector<std::string_view> keys;
    if (spec.first_key == 0) return keys;
    const int argc = static_cast<int>(cmd.argv.size());
    int last = spec.last_key < 0 ? argc + spec.last_key : spec.last_key;
    if (last >= argc) last = argc - 1;
    for (int i = spec.first_key; i <= last; i += spec.key_step) {
        keys.push_back(cmd.argv[static_cast<std::size_t>(i)]);
    }
    return keys;
}

}  // namespace blazekv

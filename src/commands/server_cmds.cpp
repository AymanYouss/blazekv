#include <algorithm>
#include <cctype>
#include <string>

#include "blazekv/build_config.hpp"
#include "blazekv/command.hpp"
#include "blazekv/hyperloglog.hpp"
#include "blazekv/object.hpp"
#include "blazekv/server.hpp"
#include "blazekv/shard.hpp"

namespace blazekv {
namespace cmd {
namespace {
std::string lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}
}  // namespace

void ping(CommandContext& c) {
    if (c.argc() >= 2) c.reply.bulk(c.arg(1));
    else c.reply.simple_string("PONG");
}

void echo(CommandContext& c) { c.reply.bulk(c.arg(1)); }
void select(CommandContext& c) { c.reply.simple_string("OK"); }
void reset(CommandContext& c) { c.reply.simple_string("RESET"); }

void wait(CommandContext& c) { c.reply.integer(0); }

void command_(CommandContext& c) {
    if (c.argc() >= 2 && lower(c.arg(1)) == "count") {
        c.reply.integer(static_cast<std::int64_t>(CommandTable::instance().all().size()));
        return;
    }
    if (c.argc() >= 2 && lower(c.arg(1)) == "docs") {
        c.reply.array_header(0);
        return;
    }
    // COMMAND (no args): a minimal listing of name + arity + flags.
    const auto& specs = CommandTable::instance().all();
    c.reply.array_header(static_cast<std::int64_t>(specs.size()));
    for (const auto& s : specs) {
        c.reply.array_header(6);
        c.reply.bulk(s.name);
        c.reply.integer(s.arity);
        c.reply.array_header(1);
        c.reply.simple_string(s.write ? "write" : "readonly");
        c.reply.integer(s.first_key);
        c.reply.integer(s.last_key < 0 ? -1 : s.last_key);
        c.reply.integer(s.key_step);
    }
}

void hello(CommandContext& c) {
    // Reply with a flat map of server fields (works for RESP2 clients too).
    c.reply.array_header(14);
    c.reply.bulk("server");
    c.reply.bulk("blazekv");
    c.reply.bulk("version");
    c.reply.bulk(BLAZEKV_VERSION);
    c.reply.bulk("proto");
    c.reply.integer(2);
    c.reply.bulk("id");
    c.reply.integer(1);
    c.reply.bulk("mode");
    c.reply.bulk("standalone");
    c.reply.bulk("role");
    c.reply.bulk("master");
    c.reply.bulk("modules");
    c.reply.array_header(0);
}

void client(CommandContext& c) {
    std::string sub = lower(c.arg(1));
    if (sub == "id") c.reply.integer(1);
    else if (sub == "getname") c.reply.null_bulk();
    else if (sub == "setname") c.reply.simple_string("OK");
    else if (sub == "info") c.reply.bulk("id=1 addr=127.0.0.1:0 name= db=0");
    else c.reply.simple_string("OK");
}

void debug(CommandContext& c) {
    std::string sub = lower(c.arg(1));
    if (sub == "jmap" || sub == "set-active-expire" || sub == "quicklist-packed-threshold" ||
        sub == "stringmatch-len" || sub == "sleep") {
        c.reply.simple_string("OK");
    } else if (sub == "object") {
        c.reply.simple_string("Value at: encoding:raw serializedlength:0");
    } else {
        c.reply.simple_string("OK");
    }
}

void config(CommandContext& c) {
    std::string sub = lower(c.arg(1));
    if (sub == "get") {
        std::string param = c.argc() >= 3 ? lower(c.arg(2)) : "";
        const auto& cfg = c.shard.server().config();
        c.reply.array_header(2);
        c.reply.bulk(param.empty() ? "maxmemory" : param);
        if (param == "maxmemory") c.reply.bulk(std::to_string(cfg.maxmemory));
        else if (param == "save") c.reply.bulk("");
        else if (param == "appendonly") c.reply.bulk(cfg.aof_enabled ? "yes" : "no");
        else c.reply.bulk("");
    } else if (sub == "set") {
        c.reply.simple_string("OK");
    } else {
        c.reply.simple_string("OK");
    }
}

void dbsize(CommandContext& c) {
    Server& s = c.shard.server();
    std::int64_t total = 0;
    for (unsigned i = 0; i < s.shard_count(); ++i)
        total += static_cast<std::int64_t>(s.shard(i).db().size());
    c.reply.integer(total);
}

void info(CommandContext& c) {
    std::string_view section = c.argc() >= 2 ? c.arg(1) : std::string_view("default");
    c.reply.bulk(c.shard.server().render_info(section));
}

namespace {
void flush_all_shards(CommandContext& c) {
    Server& s = c.shard.server();
    for (unsigned i = 0; i < s.shard_count(); ++i) {
        Shard& sh = s.shard(i);
        if (&sh == &c.shard) sh.flush();
        else sh.post([&sh] { sh.flush(); });
    }
}
}  // namespace

void flushall(CommandContext& c) {
    flush_all_shards(c);
    c.reply.simple_string("OK");
}
void flushdb(CommandContext& c) {
    flush_all_shards(c);
    c.reply.simple_string("OK");
}

// ---------------------------------------------------------------------------
// HyperLogLog commands (registers live inside a string value)
// ---------------------------------------------------------------------------
namespace {
Object* get_hll(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::String && o->type() != ObjType::HLL) {
        type_err = true;
        return nullptr;
    }
    return o;
}
}  // namespace

void pfadd(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hll(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    std::string regs = o ? o->str() : std::string();
    bool changed = false;
    if (o == nullptr) {
        hll::ensure(regs);
        changed = true;
    }
    for (std::size_t i = 2; i < c.argc(); ++i)
        if (hll::add(regs, c.arg(i))) changed = true;
    if (changed) {
        c.db.set(c.arg(1), Object(std::move(regs)));
        c.mark_dirty();
    }
    c.reply.integer(changed ? 1 : 0);
}

void pfcount(CommandContext& c) {
    std::string merged;
    hll::ensure(merged);
    for (std::size_t i = 1; i < c.argc(); ++i) {
        Object* o = c.db.lookup(c.arg(i));
        if (o != nullptr && (o->type() == ObjType::String || o->type() == ObjType::HLL))
            hll::merge(merged, o->str());
    }
    c.reply.integer(static_cast<std::int64_t>(hll::count(merged)));
}

void pfmerge(CommandContext& c) {
    Object* d = c.db.lookup(c.arg(1));
    std::string regs = (d && (d->type() == ObjType::String || d->type() == ObjType::HLL))
                           ? d->str()
                           : std::string();
    hll::ensure(regs);
    for (std::size_t i = 1; i < c.argc(); ++i) {
        Object* o = c.db.lookup(c.arg(i));
        if (o != nullptr && (o->type() == ObjType::String || o->type() == ObjType::HLL))
            hll::merge(regs, o->str());
    }
    c.db.set(c.arg(1), Object(std::move(regs)));
    c.mark_dirty();
    c.reply.simple_string("OK");
}

}  // namespace cmd
}  // namespace blazekv

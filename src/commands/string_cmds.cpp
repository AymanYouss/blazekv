#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

// Fetches a String object, replying WRONGTYPE and returning nullptr on mismatch.
Object* get_string(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::String) {
        type_err = true;
        return nullptr;
    }
    return o;
}

void incr_by(CommandContext& c, std::int64_t delta) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    std::int64_t cur = 0;
    if (o != nullptr) {
        if (!parse_int(o->str(), cur)) {
            c.err_not_int();
            return;
        }
    }
    // Overflow check mirrors Redis: reject rather than wrap.
    if ((delta > 0 && cur > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && cur < std::numeric_limits<std::int64_t>::min() - delta)) {
        c.reply.error("ERR increment or decrement would overflow");
        return;
    }
    cur += delta;
    c.db.set(c.arg(1), Object(int_to_string(cur)));
    c.mark_dirty();
    c.reply.integer(cur);
}

}  // namespace

void get(CommandContext& c) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.miss();
        c.reply.null_bulk();
        return;
    }
    c.hit();
    c.reply.bulk(o->str());
}

void set(CommandContext& c) {
    std::string_view key = c.arg(1);
    std::string_view val = c.arg(2);
    std::uint64_t deadline = 0;
    bool nx = false, xx = false, keepttl = false, get_old = false;

    for (std::size_t i = 3; i < c.argc(); ++i) {
        std::string opt(c.arg(i));
        std::transform(opt.begin(), opt.end(), opt.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        if (opt == "nx") {
            nx = true;
        } else if (opt == "xx") {
            xx = true;
        } else if (opt == "keepttl") {
            keepttl = true;
        } else if (opt == "get") {
            get_old = true;
        } else if ((opt == "ex" || opt == "px" || opt == "exat" || opt == "pxat") &&
                   i + 1 < c.argc()) {
            std::int64_t n = 0;
            if (!parse_int(c.arg(i + 1), n) || n <= 0) {
                c.reply.error("ERR invalid expire time in 'set' command");
                return;
            }
            const std::uint64_t now = now_ms();
            if (opt == "ex") deadline = now + static_cast<std::uint64_t>(n) * 1000;
            else if (opt == "px") deadline = now + static_cast<std::uint64_t>(n);
            else if (opt == "exat") deadline = static_cast<std::uint64_t>(n) * 1000;
            else deadline = static_cast<std::uint64_t>(n);
            ++i;
        } else {
            c.err_syntax();
            return;
        }
    }

    Object* existing = c.db.lookup(key);
    if (existing != nullptr && existing->type() != ObjType::String) existing = nullptr;
    if (nx && existing != nullptr) {
        if (get_old) c.reply.bulk(existing->str());
        else c.reply.null_bulk();
        return;
    }
    if (xx && c.db.lookup(key) == nullptr) {
        if (get_old) c.reply.null_bulk();
        else c.reply.null_bulk();
        return;
    }

    std::string old;
    bool had_old = existing != nullptr;
    if (get_old && had_old) old = existing->str();

    std::uint64_t keep_deadline = keepttl ? c.db.deadline_or_zero(key) : 0;
    c.db.set(key, Object(std::string(val)));
    if (deadline != 0) c.db.set_deadline(key, deadline);
    else if (keep_deadline != 0) c.db.set_deadline(key, keep_deadline);
    c.mark_dirty();

    if (get_old) {
        if (had_old) c.reply.bulk(old);
        else c.reply.null_bulk();
    } else {
        c.reply.simple_string("OK");
    }
}

void setnx(CommandContext& c) {
    if (c.db.lookup(c.arg(1)) != nullptr) {
        c.reply.integer(0);
        return;
    }
    c.db.set(c.arg(1), Object(std::string(c.arg(2))));
    c.mark_dirty();
    c.reply.integer(1);
}

void setex(CommandContext& c) {
    std::int64_t secs = 0;
    if (!parse_int(c.arg(2), secs) || secs <= 0) {
        c.reply.error("ERR invalid expire time in 'setex' command");
        return;
    }
    // psetex shares this handler but passes milliseconds; distinguish by name.
    std::string name(c.cmd.name());
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    const std::uint64_t span = name == "psetex" ? static_cast<std::uint64_t>(secs)
                                                : static_cast<std::uint64_t>(secs) * 1000;
    c.db.set(c.arg(1), Object(std::string(c.arg(3))));
    c.db.set_deadline(c.arg(1), now_ms() + span);
    c.mark_dirty();
    c.reply.simple_string("OK");
}

void getset(CommandContext& c) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) c.reply.null_bulk();
    else c.reply.bulk(o->str());
    c.db.set(c.arg(1), Object(std::string(c.arg(2))));
    c.mark_dirty();
}

void append(CommandContext& c) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.db.set(c.arg(1), Object(std::string(c.arg(2))));
        c.mark_dirty();
        c.reply.integer(static_cast<std::int64_t>(c.arg(2).size()));
        return;
    }
    o->str().append(c.arg(2));
    c.mark_dirty();
    c.reply.integer(static_cast<std::int64_t>(o->str().size()));
}

void strlen_(CommandContext& c) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o == nullptr ? 0 : static_cast<std::int64_t>(o->str().size()));
}

void incr(CommandContext& c) { incr_by(c, 1); }
void decr(CommandContext& c) { incr_by(c, -1); }

void incrby(CommandContext& c) {
    std::int64_t d = 0;
    if (!parse_int(c.arg(2), d)) {
        c.err_not_int();
        return;
    }
    incr_by(c, d);
}
void decrby(CommandContext& c) {
    std::int64_t d = 0;
    if (!parse_int(c.arg(2), d)) {
        c.err_not_int();
        return;
    }
    incr_by(c, -d);
}

void incrbyfloat(CommandContext& c) {
    double delta = 0;
    if (!parse_double(c.arg(2), delta)) {
        c.err_not_float();
        return;
    }
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    double cur = 0;
    if (o != nullptr && !parse_double(o->str(), cur)) {
        c.err_not_float();
        return;
    }
    cur += delta;
    std::string s = double_to_string(cur);
    c.db.set(c.arg(1), Object(s));
    c.mark_dirty();
    c.reply.bulk(s);
}

void mget(CommandContext& c) {
    c.reply.array_header(static_cast<std::int64_t>(c.argc() - 1));
    for (std::size_t i = 1; i < c.argc(); ++i) {
        Object* o = c.db.lookup(c.arg(i));
        if (o != nullptr && o->type() == ObjType::String) {
            c.hit();
            c.reply.bulk(o->str());
        } else {
            c.miss();
            c.reply.null_bulk();
        }
    }
}

void mset(CommandContext& c) {
    if (c.argc() % 2 != 1) {
        c.err_wrong_args();
        return;
    }
    for (std::size_t i = 1; i + 1 < c.argc(); i += 2) {
        c.db.set(c.arg(i), Object(std::string(c.arg(i + 1))));
    }
    c.mark_dirty();
    c.reply.simple_string("OK");
}

void getrange(CommandContext& c) {
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.bulk("");
        return;
    }
    const std::string& s = o->str();
    std::int64_t start = 0, end = 0;
    if (!parse_int(c.arg(2), start) || !parse_int(c.arg(3), end)) {
        c.err_not_int();
        return;
    }
    const std::int64_t len = static_cast<std::int64_t>(s.size());
    if (start < 0) start = std::max<std::int64_t>(0, len + start);
    if (end < 0) end = len + end;
    if (end >= len) end = len - 1;
    if (start > end || len == 0) {
        c.reply.bulk("");
        return;
    }
    c.reply.bulk(std::string_view(s).substr(static_cast<std::size_t>(start),
                                            static_cast<std::size_t>(end - start + 1)));
}

void setrange(CommandContext& c) {
    std::int64_t offset = 0;
    if (!parse_int(c.arg(2), offset) || offset < 0) {
        c.err_not_int();
        return;
    }
    bool type_err = false;
    Object* o = get_string(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    std::string_view val = c.arg(3);
    std::string data = o ? o->str() : std::string();
    const std::size_t needed = static_cast<std::size_t>(offset) + val.size();
    if (needed > data.size()) data.resize(needed, '\0');
    if (!val.empty()) std::copy(val.begin(), val.end(), data.begin() + offset);
    c.db.set(c.arg(1), Object(data));
    c.mark_dirty();
    c.reply.integer(static_cast<std::int64_t>(data.size()));
}

}  // namespace cmd
}  // namespace blazekv

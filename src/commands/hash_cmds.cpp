#include <algorithm>
#include <cctype>
#include <string>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

Object* get_hash(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::Hash) {
        type_err = true;
        return nullptr;
    }
    return o;
}

bool is_hmset(const CommandContext& c) {
    std::string n(c.cmd.name());
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return n == "hmset";
}

}  // namespace

void hset(CommandContext& c) {
    if (c.argc() % 2 != 0) {
        c.err_wrong_args();
        return;
    }
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_hash());
    std::int64_t added = 0;
    for (std::size_t i = 2; i + 1 < c.argc(); i += 2) {
        auto [slot, inserted] =
            o->hash().insert_or_assign(std::string(c.arg(i)), std::string(c.arg(i + 1)));
        if (inserted) ++added;
    }
    c.mark_dirty();
    if (is_hmset(c)) c.reply.simple_string("OK");
    else c.reply.integer(added);
}

void hget(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.miss();
        c.reply.null_bulk();
        return;
    }
    std::string* v = o->hash().find(std::string(c.arg(2)));
    if (v == nullptr) {
        c.miss();
        c.reply.null_bulk();
    } else {
        c.hit();
        c.reply.bulk(*v);
    }
}

void hdel(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.integer(0);
        return;
    }
    std::int64_t removed = 0;
    for (std::size_t i = 2; i < c.argc(); ++i)
        if (o->hash().erase(std::string(c.arg(i)))) ++removed;
    if (o->hash().size() == 0) c.db.erase(c.arg(1));
    if (removed) c.mark_dirty();
    c.reply.integer(removed);
}

void hexists(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o != nullptr && o->hash().find(std::string(c.arg(2))) != nullptr ? 1 : 0);
}

void hgetall(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    c.reply.array_header(static_cast<std::int64_t>(o->hash().size()) * 2);
    o->hash().for_each([&](const std::string& k, std::string& v) {
        c.reply.bulk(k);
        c.reply.bulk(v);
    });
}

void hkeys(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    c.reply.array_header(static_cast<std::int64_t>(o->hash().size()));
    o->hash().for_each([&](const std::string& k, std::string&) { c.reply.bulk(k); });
}

void hvals(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    c.reply.array_header(static_cast<std::int64_t>(o->hash().size()));
    o->hash().for_each([&](const std::string&, std::string& v) { c.reply.bulk(v); });
}

void hlen(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o == nullptr ? 0 : static_cast<std::int64_t>(o->hash().size()));
}

void hincrby(CommandContext& c) {
    std::int64_t delta = 0;
    if (!parse_int(c.arg(3), delta)) {
        c.err_not_int();
        return;
    }
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_hash());
    std::string field(c.arg(2));
    std::int64_t cur = 0;
    if (std::string* v = o->hash().find(field)) {
        if (!parse_int(*v, cur)) {
            c.reply.error("ERR hash value is not an integer");
            return;
        }
    }
    cur += delta;
    o->hash().insert_or_assign(std::move(field), int_to_string(cur));
    c.mark_dirty();
    c.reply.integer(cur);
}

void hmget(CommandContext& c) {
    bool type_err = false;
    Object* o = get_hash(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.array_header(static_cast<std::int64_t>(c.argc() - 2));
    for (std::size_t i = 2; i < c.argc(); ++i) {
        std::string* v = o ? o->hash().find(std::string(c.arg(i))) : nullptr;
        if (v == nullptr) c.reply.null_bulk();
        else c.reply.bulk(*v);
    }
}

}  // namespace cmd
}  // namespace blazekv

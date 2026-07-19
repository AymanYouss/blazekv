#include <algorithm>
#include <string>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

Object* get_list(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::List) {
        type_err = true;
        return nullptr;
    }
    return o;
}

void push(CommandContext& c, bool head) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_list());
    for (std::size_t i = 2; i < c.argc(); ++i) {
        if (head) o->list().push_front(std::string(c.arg(i)));
        else o->list().push_back(std::string(c.arg(i)));
    }
    c.mark_dirty();
    c.reply.integer(static_cast<std::int64_t>(o->list().size()));
}

void pop(CommandContext& c, bool head) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    bool has_count = c.argc() == 3;
    std::int64_t count = 1;
    if (has_count && (!parse_int(c.arg(2), count) || count < 0)) {
        c.err_not_int();
        return;
    }
    if (o == nullptr) {
        if (has_count) c.reply.null_array();
        else c.reply.null_bulk();
        return;
    }
    auto& l = o->list();
    std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(count), l.size());
    if (has_count) c.reply.array_header(static_cast<std::int64_t>(n));
    for (std::size_t i = 0; i < n; ++i) {
        if (head) {
            c.reply.bulk(l.front());
            l.pop_front();
        } else {
            c.reply.bulk(l.back());
            l.pop_back();
        }
    }
    if (!has_count && n == 0) c.reply.null_bulk();
    if (l.empty()) c.db.erase(c.arg(1));
    c.mark_dirty();
}

}  // namespace

void lpush(CommandContext& c) { push(c, true); }
void rpush(CommandContext& c) { push(c, false); }
void lpop(CommandContext& c) { pop(c, true); }
void rpop(CommandContext& c) { pop(c, false); }

void llen(CommandContext& c) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o == nullptr ? 0 : static_cast<std::int64_t>(o->list().size()));
}

void lrange(CommandContext& c) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    std::int64_t start = 0, stop = 0;
    if (!parse_int(c.arg(2), start) || !parse_int(c.arg(3), stop)) {
        c.err_not_int();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    auto& l = o->list();
    const std::int64_t len = static_cast<std::int64_t>(l.size());
    if (start < 0) start = std::max<std::int64_t>(0, len + start);
    if (stop < 0) stop = len + stop;
    if (stop >= len) stop = len - 1;
    if (start > stop || len == 0) {
        c.reply.array_header(0);
        return;
    }
    c.reply.array_header(stop - start + 1);
    for (std::int64_t i = start; i <= stop; ++i)
        c.reply.bulk(l[static_cast<std::size_t>(i)]);
}

void lindex(CommandContext& c) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    std::int64_t idx = 0;
    if (!parse_int(c.arg(2), idx)) {
        c.err_not_int();
        return;
    }
    if (o == nullptr) {
        c.reply.null_bulk();
        return;
    }
    auto& l = o->list();
    const std::int64_t len = static_cast<std::int64_t>(l.size());
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) c.reply.null_bulk();
    else c.reply.bulk(l[static_cast<std::size_t>(idx)]);
}

void lset(CommandContext& c) {
    bool type_err = false;
    Object* o = get_list(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.error("ERR no such key");
        return;
    }
    std::int64_t idx = 0;
    if (!parse_int(c.arg(2), idx)) {
        c.err_not_int();
        return;
    }
    auto& l = o->list();
    const std::int64_t len = static_cast<std::int64_t>(l.size());
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) {
        c.reply.error("ERR index out of range");
        return;
    }
    l[static_cast<std::size_t>(idx)] = std::string(c.arg(3));
    c.mark_dirty();
    c.reply.simple_string("OK");
}

}  // namespace cmd
}  // namespace blazekv

#include <algorithm>
#include <string>
#include <vector>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

Object* get_set(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::Set) {
        type_err = true;
        return nullptr;
    }
    return o;
}

}  // namespace

void sadd(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_set());
    std::int64_t added = 0;
    for (std::size_t i = 2; i < c.argc(); ++i) {
        auto [slot, inserted] = o->set().insert_or_assign(std::string(c.arg(i)), std::uint8_t{1});
        if (inserted) ++added;
    }
    if (added) c.mark_dirty();
    c.reply.integer(added);
}

void srem(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
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
        if (o->set().erase(std::string(c.arg(i)))) ++removed;
    if (o->set().size() == 0) c.db.erase(c.arg(1));
    if (removed) c.mark_dirty();
    c.reply.integer(removed);
}

void sismember(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o != nullptr && o->set().find(std::string(c.arg(2))) != nullptr ? 1 : 0);
}

void scard(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o == nullptr ? 0 : static_cast<std::int64_t>(o->set().size()));
}

void smembers(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    c.reply.array_header(static_cast<std::int64_t>(o->set().size()));
    o->set().for_each([&](const std::string& m, std::uint8_t&) { c.reply.bulk(m); });
}

void spop(CommandContext& c) {
    bool type_err = false;
    Object* o = get_set(c, c.arg(1), type_err);
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
        if (has_count) c.reply.array_header(0);
        else c.reply.null_bulk();
        return;
    }
    std::vector<std::string> picked;
    const std::size_t want = has_count ? static_cast<std::size_t>(count) : 1;
    o->set().for_each([&](const std::string& m, std::uint8_t&) {
        if (picked.size() < want) picked.push_back(m);
    });
    for (const auto& m : picked) o->set().erase(m);
    if (o->set().size() == 0) c.db.erase(c.arg(1));
    if (!picked.empty()) c.mark_dirty();

    if (has_count) {
        c.reply.array_header(static_cast<std::int64_t>(picked.size()));
        for (const auto& m : picked) c.reply.bulk(m);
    } else if (picked.empty()) {
        c.reply.null_bulk();
    } else {
        c.reply.bulk(picked.front());
    }
}

}  // namespace cmd
}  // namespace blazekv

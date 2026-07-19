#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

Object* get_zset(CommandContext& c, std::string_view key, bool& type_err) {
    type_err = false;
    Object* o = c.db.lookup(key);
    if (o == nullptr) return nullptr;
    if (o->type() != ObjType::ZSet) {
        type_err = true;
        return nullptr;
    }
    return o;
}

// Parses a score bound that may carry a '(' exclusive prefix or be +/-inf.
bool parse_bound(std::string_view s, double& out, bool& exclusive) {
    exclusive = false;
    if (!s.empty() && s[0] == '(') {
        exclusive = true;
        s.remove_prefix(1);
    }
    return parse_double(s, out);
}

}  // namespace

void zadd(CommandContext& c) {
    std::size_t i = 2;
    bool nx = false, xx = false, ch = false, incr = false, gt = false, lt = false;
    for (; i < c.argc(); ++i) {
        std::string opt(c.arg(i));
        std::transform(opt.begin(), opt.end(), opt.begin(),
                       [](unsigned char x) { return std::tolower(x); });
        if (opt == "nx") nx = true;
        else if (opt == "xx") xx = true;
        else if (opt == "ch") ch = true;
        else if (opt == "incr") incr = true;
        else if (opt == "gt") gt = true;
        else if (opt == "lt") lt = true;
        else break;
    }
    if (i >= c.argc() || (c.argc() - i) % 2 != 0) {
        c.err_syntax();
        return;
    }
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_zset());
    ZSetType& z = o->zset();

    std::int64_t added = 0, changed = 0;
    double last_score = 0;
    bool did = false;
    for (; i + 1 < c.argc(); i += 2) {
        double score = 0;
        if (!parse_double(c.arg(i), score)) {
            c.err_not_float();
            return;
        }
        std::string member(c.arg(i + 1));
        double* cur = z.dict.find(member);
        if (incr) {
            double base = cur ? *cur : 0.0;
            score += base;
        }
        if (cur == nullptr) {
            if (xx) continue;
            z.set(member, score);
            ++added;
            ++changed;
        } else {
            if (nx) continue;
            if (gt && score <= *cur) continue;
            if (lt && score >= *cur) continue;
            if (*cur != score) {
                z.set(member, score);
                ++changed;
            }
        }
        last_score = score;
        did = true;
    }
    if (added || changed) c.mark_dirty();
    if (o->zset().size() == 0) c.db.erase(c.arg(1));

    if (incr) {
        if (did) c.reply.double_reply(last_score);
        else c.reply.null_bulk();
    } else {
        c.reply.integer(ch ? changed : added);
    }
}

void zrem(CommandContext& c) {
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
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
        if (o->zset().remove(std::string(c.arg(i)))) ++removed;
    if (o->zset().size() == 0) c.db.erase(c.arg(1));
    if (removed) c.mark_dirty();
    c.reply.integer(removed);
}

void zscore(CommandContext& c) {
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.null_bulk();
        return;
    }
    double* s = o->zset().dict.find(std::string(c.arg(2)));
    if (s == nullptr) c.reply.null_bulk();
    else c.reply.double_reply(*s);
}

void zcard(CommandContext& c) {
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    c.reply.integer(o == nullptr ? 0 : static_cast<std::int64_t>(o->zset().size()));
}

void zrank(CommandContext& c) {
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.null_bulk();
        return;
    }
    std::string member(c.arg(2));
    double* s = o->zset().dict.find(member);
    if (s == nullptr) {
        c.reply.null_bulk();
        return;
    }
    std::uint32_t rank = o->zset().sl.rank_of(member, *s);
    c.reply.integer(static_cast<std::int64_t>(rank) - 1);  // ZRANK is 0-based
}

void zincrby(CommandContext& c) {
    double delta = 0;
    if (!parse_double(c.arg(2), delta)) {
        c.err_not_float();
        return;
    }
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) o = &c.db.set(c.arg(1), Object::make_zset());
    std::string member(c.arg(3));
    double* cur = o->zset().dict.find(member);
    double v = (cur ? *cur : 0.0) + delta;
    o->zset().set(member, v);
    c.mark_dirty();
    c.reply.double_reply(v);
}

void zrange(CommandContext& c) {
    bool withscores = false, rev = false;
    for (std::size_t i = 4; i < c.argc(); ++i) {
        std::string opt(c.arg(i));
        std::transform(opt.begin(), opt.end(), opt.begin(),
                       [](unsigned char x) { return std::tolower(x); });
        if (opt == "withscores") withscores = true;
        else if (opt == "rev") rev = true;
    }
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
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
    ZSetType& z = o->zset();
    const std::int64_t len = static_cast<std::int64_t>(z.size());
    if (start < 0) start = std::max<std::int64_t>(0, len + start);
    if (stop < 0) stop = len + stop;
    if (stop >= len) stop = len - 1;
    if (start > stop || len == 0) {
        c.reply.array_header(0);
        return;
    }
    const std::int64_t count = stop - start + 1;
    c.reply.array_header(withscores ? count * 2 : count);
    for (std::int64_t i = start; i <= stop; ++i) {
        std::uint32_t rank = rev ? static_cast<std::uint32_t>(len - i)
                                 : static_cast<std::uint32_t>(i + 1);
        auto* node = z.sl.at_rank(rank);
        if (node == nullptr) continue;
        c.reply.bulk(node->member);
        if (withscores) c.reply.double_reply(node->score);
    }
}

void zrangebyscore(CommandContext& c) {
    bool withscores = false;
    std::int64_t offset = 0, limit = -1;
    for (std::size_t i = 4; i < c.argc(); ++i) {
        std::string opt(c.arg(i));
        std::transform(opt.begin(), opt.end(), opt.begin(),
                       [](unsigned char x) { return std::tolower(x); });
        if (opt == "withscores") {
            withscores = true;
        } else if (opt == "limit" && i + 2 < c.argc()) {
            parse_int(c.arg(i + 1), offset);
            parse_int(c.arg(i + 2), limit);
            i += 2;
        }
    }
    double min = 0, max = 0;
    bool min_excl = false, max_excl = false;
    if (!parse_bound(c.arg(2), min, min_excl) || !parse_bound(c.arg(3), max, max_excl)) {
        c.reply.error("ERR min or max is not a float");
        return;
    }
    bool type_err = false;
    Object* o = get_zset(c, c.arg(1), type_err);
    if (type_err) {
        c.err_wrong_type();
        return;
    }
    if (o == nullptr) {
        c.reply.array_header(0);
        return;
    }
    ZSetType& z = o->zset();
    std::vector<std::pair<std::string, double>> out;
    for (auto* n = z.sl.first_gte(min); n != nullptr; n = n->level[0].forward) {
        if (min_excl && n->score <= min) continue;
        if (n->score > max || (max_excl && n->score >= max)) break;
        out.emplace_back(n->member, n->score);
    }
    if (offset > 0) {
        if (static_cast<std::size_t>(offset) >= out.size()) out.clear();
        else out.erase(out.begin(), out.begin() + offset);
    }
    if (limit >= 0 && static_cast<std::size_t>(limit) < out.size()) out.resize(static_cast<std::size_t>(limit));

    c.reply.array_header(withscores ? static_cast<std::int64_t>(out.size()) * 2
                                    : static_cast<std::int64_t>(out.size()));
    for (auto& [m, s] : out) {
        c.reply.bulk(m);
        if (withscores) c.reply.double_reply(s);
    }
}

}  // namespace cmd
}  // namespace blazekv

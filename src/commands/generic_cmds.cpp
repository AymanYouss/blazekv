#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "blazekv/command.hpp"
#include "blazekv/object.hpp"

namespace blazekv {
namespace cmd {
namespace {

// Redis-compatible glob matcher: supports *, ?, [..] classes and \ escaping.
bool glob_match(std::string_view pat, std::string_view str) {
    std::size_t p = 0, s = 0, star_p = std::string_view::npos, star_s = 0;
    while (s < str.size()) {
        if (p < pat.size() && (pat[p] == '?' || pat[p] == str[s])) {
            ++p;
            ++s;
        } else if (p < pat.size() && pat[p] == '*') {
            star_p = p++;
            star_s = s;
        } else if (p < pat.size() && pat[p] == '[') {
            std::size_t q = p + 1;
            bool negate = q < pat.size() && (pat[q] == '^' || pat[q] == '!');
            if (negate) ++q;
            bool matched = false;
            while (q < pat.size() && pat[q] != ']') {
                if (q + 2 < pat.size() && pat[q + 1] == '-' && pat[q + 2] != ']') {
                    if (str[s] >= pat[q] && str[s] <= pat[q + 2]) matched = true;
                    q += 3;
                } else {
                    if (pat[q] == str[s]) matched = true;
                    ++q;
                }
            }
            if (matched != negate) {
                p = (q < pat.size() ? q + 1 : q);
                ++s;
            } else if (star_p != std::string_view::npos) {
                p = star_p + 1;
                s = ++star_s;
            } else {
                return false;
            }
        } else if (p < pat.size() && pat[p] == '\\' && p + 1 < pat.size() && pat[p + 1] == str[s]) {
            p += 2;
            ++s;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

}  // namespace

void del(CommandContext& c) {
    std::int64_t removed = 0;
    for (std::size_t i = 1; i < c.argc(); ++i)
        if (c.db.erase(c.arg(i))) ++removed;
    if (removed) c.mark_dirty();
    c.reply.integer(removed);
}

void unlink(CommandContext& c) { del(c); }

void exists(CommandContext& c) {
    std::int64_t count = 0;
    for (std::size_t i = 1; i < c.argc(); ++i)
        if (c.db.exists(c.arg(i))) ++count;
    c.reply.integer(count);
}

namespace {
void set_expire(CommandContext& c, std::uint64_t deadline_ms) {
    if (!c.db.exists(c.arg(1))) {
        c.reply.integer(0);
        return;
    }
    c.db.set_deadline(c.arg(1), deadline_ms);
    c.mark_dirty();
    c.reply.integer(1);
}
}  // namespace

void expire(CommandContext& c) {
    std::int64_t secs = 0;
    if (!parse_int(c.arg(2), secs)) {
        c.err_not_int();
        return;
    }
    set_expire(c, now_ms() + static_cast<std::uint64_t>(std::max<std::int64_t>(0, secs)) * 1000);
}

void pexpire(CommandContext& c) {
    std::int64_t ms = 0;
    if (!parse_int(c.arg(2), ms)) {
        c.err_not_int();
        return;
    }
    set_expire(c, now_ms() + static_cast<std::uint64_t>(std::max<std::int64_t>(0, ms)));
}

void expireat(CommandContext& c) {
    std::int64_t at = 0;
    if (!parse_int(c.arg(2), at)) {
        c.err_not_int();
        return;
    }
    set_expire(c, static_cast<std::uint64_t>(std::max<std::int64_t>(0, at)) * 1000);
}

void ttl(CommandContext& c) {
    std::int64_t ms = c.db.ttl_ms(c.arg(1));
    if (ms < 0) c.reply.integer(ms);
    else c.reply.integer((ms + 999) / 1000);
}

void pttl(CommandContext& c) { c.reply.integer(c.db.ttl_ms(c.arg(1))); }

void persist(CommandContext& c) {
    if (!c.db.exists(c.arg(1))) {
        c.reply.integer(0);
        return;
    }
    bool removed = c.db.persist(c.arg(1));
    if (removed) c.mark_dirty();
    c.reply.integer(removed ? 1 : 0);
}

void type(CommandContext& c) {
    Object* o = c.db.lookup(c.arg(1));
    c.reply.simple_string(o == nullptr ? "none" : type_name(o->type()));
}

void keys(CommandContext& c) {
    std::string_view pat = c.arg(1);
    std::vector<std::string_view> matches;
    const std::uint64_t now = now_ms();
    c.db.for_each([&](const std::string& key, Object&) {
        std::uint64_t dl = c.db.deadline_or_zero(key);
        if (dl != 0 && dl <= now) return;  // skip logically-expired keys
        if (glob_match(pat, key)) matches.push_back(key);
    });
    c.reply.array_header(static_cast<std::int64_t>(matches.size()));
    for (auto m : matches) c.reply.bulk(m);
}

void scan(CommandContext& c) {
    std::string_view match = "*";
    ObjType type_filter{};
    bool has_type = false;
    for (std::size_t i = 2; i + 1 < c.argc(); i += 2) {
        std::string opt(c.arg(i));
        std::transform(opt.begin(), opt.end(), opt.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        if (opt == "match") {
            match = c.arg(i + 1);
        } else if (opt == "type") {
            has_type = true;
            std::string t(c.arg(i + 1));
            if (t == "string") type_filter = ObjType::String;
            else if (t == "list") type_filter = ObjType::List;
            else if (t == "hash") type_filter = ObjType::Hash;
            else if (t == "set") type_filter = ObjType::Set;
            else if (t == "zset") type_filter = ObjType::ZSet;
            else has_type = false;
        }
        // COUNT is advisory; a full, consistent pass is a valid SCAN implementation.
    }
    std::vector<std::string_view> out;
    const std::uint64_t now = now_ms();
    c.db.for_each([&](const std::string& key, Object& o) {
        std::uint64_t dl = c.db.deadline_or_zero(key);
        if (dl != 0 && dl <= now) return;
        if (has_type && o.type() != type_filter) return;
        if (glob_match(match, key)) out.push_back(key);
    });
    c.reply.array_header(2);
    c.reply.bulk("0");  // cursor 0: iteration complete
    c.reply.array_header(static_cast<std::int64_t>(out.size()));
    for (auto k : out) c.reply.bulk(k);
}

void rename(CommandContext& c) {
    Object* src = c.db.lookup(c.arg(1));
    if (src == nullptr) {
        c.reply.error("ERR no such key");
        return;
    }
    std::uint64_t dl = c.db.deadline_or_zero(c.arg(1));
    Object moved = std::move(*src);
    c.db.erase(c.arg(1));
    c.db.set(c.arg(2), std::move(moved));
    if (dl != 0) c.db.set_deadline(c.arg(2), dl);
    c.mark_dirty();
    c.reply.simple_string("OK");
}

void randomkey(CommandContext& c) {
    std::string picked;
    bool found = false;
    c.db.for_each([&](const std::string& key, Object&) {
        if (!found) {
            picked = key;
            found = true;
        }
    });
    if (found) c.reply.bulk(picked);
    else c.reply.null_bulk();
}

void object_(CommandContext& c) {
    std::string sub(c.arg(1));
    std::transform(sub.begin(), sub.end(), sub.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    if (c.argc() < 3) {
        c.err_wrong_args();
        return;
    }
    Object* o = c.db.lookup(c.arg(2));
    if (o == nullptr) {
        c.reply.error("ERR no such key");
        return;
    }
    if (sub == "encoding") {
        const char* enc = "raw";
        switch (o->type()) {
            case ObjType::String: {
                std::int64_t iv;
                enc = parse_int(o->str(), iv) ? "int" : "embstr";
                break;
            }
            case ObjType::List: enc = "quicklist"; break;
            case ObjType::Hash: enc = "hashtable"; break;
            case ObjType::Set: enc = "hashtable"; break;
            case ObjType::ZSet: enc = "skiplist"; break;
            case ObjType::HLL: enc = "raw"; break;
        }
        c.reply.bulk(enc);
    } else if (sub == "refcount") {
        c.reply.integer(1);
    } else if (sub == "idletime") {
        c.reply.integer(0);
    } else if (sub == "freq") {
        c.reply.integer(0);
    } else {
        c.err_syntax();
    }
}

}  // namespace cmd
}  // namespace blazekv

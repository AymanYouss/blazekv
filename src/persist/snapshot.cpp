#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "blazekv/persist.hpp"

namespace blazekv {
namespace snapshot {
namespace {

constexpr char kMagic[8] = {'B', 'L', 'A', 'Z', 'E', 'K', 'V', '1'};

struct Writer {
    FILE* f;
    bool ok = true;
    void bytes(const void* p, std::size_t n) {
        if (ok && std::fwrite(p, 1, n, f) != n) ok = false;
    }
    void u8(std::uint8_t v) { bytes(&v, 1); }
    void u32(std::uint32_t v) { bytes(&v, 4); }
    void u64(std::uint64_t v) { bytes(&v, 8); }
    void dbl(double v) { bytes(&v, 8); }
    void str(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }
};

struct Reader {
    FILE* f;
    bool ok = true;
    bool bytes(void* p, std::size_t n) {
        if (!ok) return false;
        if (std::fread(p, 1, n, f) != n) {
            ok = false;
            return false;
        }
        return true;
    }
    std::uint8_t u8() {
        std::uint8_t v = 0;
        bytes(&v, 1);
        return v;
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        bytes(&v, 4);
        return v;
    }
    std::uint64_t u64() {
        std::uint64_t v = 0;
        bytes(&v, 8);
        return v;
    }
    double dbl() {
        double v = 0;
        bytes(&v, 8);
        return v;
    }
    std::string str() {
        std::uint32_t n = u32();
        if (!ok || n > (1u << 30)) {
            ok = false;
            return {};
        }
        std::string s;
        s.resize(n);
        bytes(s.data(), n);
        return s;
    }
};

void write_object(Writer& w, const std::string& key, std::uint64_t deadline, Object& obj) {
    w.u8(static_cast<std::uint8_t>(obj.type()));
    w.str(key);
    w.u64(deadline);
    switch (obj.type()) {
        case ObjType::String:
        case ObjType::HLL:
            w.str(obj.str());
            break;
        case ObjType::List: {
            auto& l = obj.list();
            w.u32(static_cast<std::uint32_t>(l.size()));
            for (auto& e : l) w.str(e);
            break;
        }
        case ObjType::Hash: {
            auto& h = obj.hash();
            w.u32(static_cast<std::uint32_t>(h.size()));
            h.for_each([&](const std::string& k, std::string& v) {
                w.str(k);
                w.str(v);
            });
            break;
        }
        case ObjType::Set: {
            auto& s = obj.set();
            w.u32(static_cast<std::uint32_t>(s.size()));
            s.for_each([&](const std::string& m, std::uint8_t&) { w.str(m); });
            break;
        }
        case ObjType::ZSet: {
            auto& z = obj.zset();
            w.u32(static_cast<std::uint32_t>(z.size()));
            z.dict.for_each([&](const std::string& m, double& score) {
                w.str(m);
                w.dbl(score);
            });
            break;
        }
    }
}

bool read_object(Reader& r, Keyspace& ks) {
    auto type = static_cast<ObjType>(r.u8());
    std::string key = r.str();
    std::uint64_t deadline = r.u64();
    if (!r.ok) return false;

    // Drop keys whose stored TTL has already elapsed on load.
    const bool expired = deadline != 0 && deadline <= now_ms();

    switch (type) {
        case ObjType::String:
        case ObjType::HLL: {
            // Both are stored as raw string bytes; the type tag is informational
            // (HLL registers live inside an ordinary string value).
            std::string val = r.str();
            if (!expired && r.ok) ks.set(key, Object(std::move(val)));
            break;
        }
        case ObjType::List: {
            std::uint32_t n = r.u32();
            Object o = Object::make_list();
            for (std::uint32_t i = 0; i < n && r.ok; ++i) o.list().push_back(r.str());
            if (!expired && r.ok) ks.set(key, std::move(o));
            break;
        }
        case ObjType::Hash: {
            std::uint32_t n = r.u32();
            Object o = Object::make_hash();
            for (std::uint32_t i = 0; i < n && r.ok; ++i) {
                std::string f = r.str();
                std::string v = r.str();
                o.hash().insert_or_assign(std::move(f), std::move(v));
            }
            if (!expired && r.ok) ks.set(key, std::move(o));
            break;
        }
        case ObjType::Set: {
            std::uint32_t n = r.u32();
            Object o = Object::make_set();
            for (std::uint32_t i = 0; i < n && r.ok; ++i)
                o.set().insert_or_assign(r.str(), std::uint8_t{1});
            if (!expired && r.ok) ks.set(key, std::move(o));
            break;
        }
        case ObjType::ZSet: {
            std::uint32_t n = r.u32();
            Object o = Object::make_zset();
            for (std::uint32_t i = 0; i < n && r.ok; ++i) {
                std::string m = r.str();
                double sc = r.dbl();
                o.zset().set(m, sc);
            }
            if (!expired && r.ok) ks.set(key, std::move(o));
            break;
        }
        default:
            r.ok = false;
            return false;
    }
    if (!expired && deadline != 0 && r.ok) ks.set_deadline(key, deadline);
    return r.ok;
}

}  // namespace

bool save(const std::string& path, Keyspace& ks, std::string& err) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        err = "cannot open snapshot temp file";
        return false;
    }
    Writer w{f};
    w.bytes(kMagic, sizeof(kMagic));
    w.u64(ks.size());
    ks.for_each([&](const std::string& key, Object& obj) {
        write_object(w, key, ks.deadline_or_zero(key), obj);
    });
    std::fflush(f);
    std::fclose(f);
    if (!w.ok) {
        err = "snapshot write failed";
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "snapshot rename failed";
        return false;
    }
    return true;
}

bool load(const std::string& path, Keyspace& ks, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return true;  // no snapshot yet: empty start is success
    Reader r{f};
    char magic[8];
    if (!r.bytes(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        err = "bad snapshot magic";
        std::fclose(f);
        return false;
    }
    std::uint64_t count = r.u64();
    for (std::uint64_t i = 0; i < count && r.ok; ++i) {
        if (!read_object(r, ks)) break;
    }
    std::fclose(f);
    if (!r.ok) {
        err = "truncated or corrupt snapshot";
        return false;
    }
    return true;
}

}  // namespace snapshot
}  // namespace blazekv

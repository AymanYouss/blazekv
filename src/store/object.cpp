#include "blazekv/object.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace blazekv {

const char* type_name(ObjType t) noexcept {
    switch (t) {
        case ObjType::String:
            return "string";
        case ObjType::List:
            return "list";
        case ObjType::Hash:
            return "hash";
        case ObjType::Set:
            return "set";
        case ObjType::ZSet:
            return "zset";
        case ObjType::HLL:
            return "string";  // HLL is a string subtype in Redis
    }
    return "unknown";
}

std::size_t Object::memory_usage() const {
    // A deliberately approximate model: enough to drive INFO memory reporting and
    // maxmemory eviction decisions without walking every element on the hot path.
    constexpr std::size_t kOverhead = 48;
    switch (type_) {
        case ObjType::String:
        case ObjType::HLL:
            return kOverhead + std::get<std::string>(data_).capacity();
        case ObjType::List: {
            std::size_t n = kOverhead;
            for (const auto& s : *std::get<std::unique_ptr<ListType>>(data_)) n += 16 + s.size();
            return n;
        }
        case ObjType::Hash: {
            auto& h = *std::get<std::unique_ptr<HashType>>(data_);
            return kOverhead + h.memory_bytes();
        }
        case ObjType::Set: {
            auto& s = *std::get<std::unique_ptr<SetType>>(data_);
            return kOverhead + s.memory_bytes();
        }
        case ObjType::ZSet: {
            auto& z = *std::get<std::unique_ptr<ZSetType>>(data_);
            return kOverhead + z.dict.memory_bytes() + z.size() * 64;
        }
    }
    return kOverhead;
}

bool parse_int(std::string_view s, std::int64_t& out) noexcept {
    if (s.empty()) return false;
    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{} && res.ptr == s.data() + s.size();
}

bool parse_double(std::string_view s, double& out) noexcept {
    if (s.empty()) return false;
    if (s == "inf" || s == "+inf" || s == "Inf" || s == "+Inf") {
        out = std::numeric_limits<double>::infinity();
        return true;
    }
    if (s == "-inf" || s == "-Inf") {
        out = -std::numeric_limits<double>::infinity();
        return true;
    }
    // std::from_chars for double is not universally available in libc++ yet, so
    // fall back to strtod with strict end-pointer validation.
    std::string tmp(s);
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(tmp.c_str(), &end);
    if (end != tmp.c_str() + tmp.size() || errno == ERANGE) return false;
    out = v;
    return true;
}

std::string int_to_string(std::int64_t v) {
    char tmp[24];
    auto res = std::to_chars(tmp, tmp + sizeof(tmp), v);
    return std::string(tmp, static_cast<std::size_t>(res.ptr - tmp));
}

std::string double_to_string(double v) {
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    if (std::isnan(v)) return "nan";
    // Redis prints doubles with up to 17 significant digits, trimming trailing zeros.
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf, static_cast<std::size_t>(n));
}

}  // namespace blazekv

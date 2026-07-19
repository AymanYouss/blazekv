#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

#include "blazekv/common.hpp"

namespace blazekv {

// wyhash-style 64-bit hash: fast, high-quality, stable across runs so that key
// -> shard routing is deterministic on every core. Public-domain algorithm.
namespace detail {

BLAZEKV_ALWAYS_INLINE std::uint64_t wy_read8(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
BLAZEKV_ALWAYS_INLINE std::uint64_t wy_read4(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
BLAZEKV_ALWAYS_INLINE std::uint64_t wy_read3(const std::uint8_t* p, std::size_t k) {
    return (static_cast<std::uint64_t>(p[0]) << 16) |
           (static_cast<std::uint64_t>(p[k >> 1]) << 8) | p[k - 1];
}
BLAZEKV_ALWAYS_INLINE void wy_mum(std::uint64_t* a, std::uint64_t* b) {
    __uint128_t r = static_cast<__uint128_t>(*a) * *b;
    *a = static_cast<std::uint64_t>(r);
    *b = static_cast<std::uint64_t>(r >> 64);
}
BLAZEKV_ALWAYS_INLINE std::uint64_t wy_mix(std::uint64_t a, std::uint64_t b) {
    wy_mum(&a, &b);
    return a ^ b;
}

}  // namespace detail

inline constexpr std::uint64_t kWySeed = 0x9E3779B97F4A7C15ull;

inline std::uint64_t hash_bytes(std::string_view key, std::uint64_t seed = kWySeed) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(key.data());
    std::size_t len = key.size();
    constexpr std::uint64_t s0 = 0xa0761d6478bd642full;
    constexpr std::uint64_t s1 = 0xe7037ed1a0b428dbull;
    constexpr std::uint64_t s2 = 0x8ebc6af09c88c6e3ull;
    seed ^= s0;
    std::uint64_t a, b;
    if (BLAZEKV_LIKELY(len <= 16)) {
        if (BLAZEKV_LIKELY(len >= 4)) {
            a = (detail::wy_read4(p) << 32) | detail::wy_read4(p + ((len >> 3) << 2));
            b = (detail::wy_read4(p + len - 4) << 32) |
                detail::wy_read4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = detail::wy_read3(p, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        std::size_t i = len;
        const std::uint8_t* q = p;
        std::uint64_t see1 = seed;
        while (i > 16) {
            seed = detail::wy_mix(detail::wy_read8(q) ^ s1, detail::wy_read8(q + 8) ^ seed);
            i -= 16;
            q += 16;
        }
        a = detail::wy_read8(q + i - 16);
        b = detail::wy_read8(q + i - 8);
        seed ^= see1;
    }
    a ^= s1;
    b ^= seed;
    detail::wy_mum(&a, &b);
    return detail::wy_mix(a ^ s0 ^ len, b ^ s2);
}

struct BytesHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return hash_bytes(s); }
};

}  // namespace blazekv

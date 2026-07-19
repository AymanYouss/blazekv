#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blazekv {

#if defined(__GNUC__) || defined(__clang__)
#define BLAZEKV_LIKELY(x) __builtin_expect(!!(x), 1)
#define BLAZEKV_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BLAZEKV_ALWAYS_INLINE inline __attribute__((always_inline))
#define BLAZEKV_PREFETCH(addr) __builtin_prefetch(addr)
#else
#define BLAZEKV_LIKELY(x) (x)
#define BLAZEKV_UNLIKELY(x) (x)
#define BLAZEKV_ALWAYS_INLINE inline
#define BLAZEKV_PREFETCH(addr) ((void)0)
#endif

// Cache-line size used to pad shared-nothing structures against false sharing.
inline constexpr std::size_t kCacheLine = 64;

using Bytes = std::string;
using BytesView = std::string_view;

// Wall-clock milliseconds since the Unix epoch. Used for absolute TTL deadlines
// so they survive snapshots/AOF and are comparable to Redis PEXPIREAT semantics.
inline std::uint64_t now_ms() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Monotonic microseconds, for latency measurement (never goes backwards).
inline std::uint64_t now_us() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace blazekv

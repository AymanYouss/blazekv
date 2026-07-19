#include "blazekv/hyperloglog.hpp"

#include <cmath>

#include "blazekv/hash.hpp"

namespace blazekv {
namespace hll {
namespace {

// Rank = position of the leftmost set bit in the (64-kPrecision)-bit tail, +1.
std::uint8_t rank(std::uint64_t hash) {
    const std::uint64_t tail = hash >> kPrecision;
    const int max_rank = 64 - kPrecision + 1;
    if (tail == 0) return static_cast<std::uint8_t>(max_rank);
    return static_cast<std::uint8_t>(__builtin_ctzll(tail) + 1);
}

double alpha() {
    // Bias correction constant for m registers.
    return 0.7213 / (1.0 + 1.079 / static_cast<double>(kRegisters));
}

}  // namespace

void ensure(std::string& regs) {
    if (regs.size() != kRegisters) regs.assign(kRegisters, '\0');
}

bool add(std::string& regs, std::string_view element) {
    ensure(regs);
    const std::uint64_t h = hash_bytes(element, 0xADC83B19ULL);
    const std::size_t idx = h & (kRegisters - 1);
    const std::uint8_t r = rank(h);
    auto* reg = reinterpret_cast<std::uint8_t*>(regs.data());
    if (r > reg[idx]) {
        reg[idx] = r;
        return true;
    }
    return false;
}

std::uint64_t count(const std::string& regs) {
    if (regs.size() != kRegisters) return 0;
    const auto* reg = reinterpret_cast<const std::uint8_t*>(regs.data());
    double sum = 0.0;
    std::size_t zeros = 0;
    for (std::size_t i = 0; i < kRegisters; ++i) {
        sum += std::ldexp(1.0, -static_cast<int>(reg[i]));  // 2^-reg[i]
        if (reg[i] == 0) ++zeros;
    }
    const double m = static_cast<double>(kRegisters);
    double estimate = alpha() * m * m / sum;

    if (estimate <= 2.5 * m && zeros != 0) {
        // Small-range correction: linear counting.
        estimate = m * std::log(m / static_cast<double>(zeros));
    }
    return static_cast<std::uint64_t>(estimate + 0.5);
}

void merge(std::string& dst, const std::string& src) {
    ensure(dst);
    if (src.size() != kRegisters) return;
    auto* d = reinterpret_cast<std::uint8_t*>(dst.data());
    const auto* s = reinterpret_cast<const std::uint8_t*>(src.data());
    for (std::size_t i = 0; i < kRegisters; ++i)
        if (s[i] > d[i]) d[i] = s[i];
}

}  // namespace hll
}  // namespace blazekv

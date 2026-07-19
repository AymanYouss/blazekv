#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace blazekv {

// A dense HyperLogLog with 2^14 registers (the same precision Redis uses),
// giving a standard error of ~0.81%. Registers are stored one-per-byte inside an
// ordinary string value, so PFADD/PFCOUNT/PFMERGE compose with the string type
// and persist through snapshots and the AOF like any other value.
namespace hll {

constexpr int kPrecision = 14;
constexpr std::size_t kRegisters = std::size_t{1} << kPrecision;

// Ensures `regs` is a valid, zero-initialized register array.
void ensure(std::string& regs);

// Adds an element; returns true if any register changed (i.e. the estimate moved).
bool add(std::string& regs, std::string_view element);

// Estimated cardinality using the bias-corrected HLL estimator with linear
// counting for small ranges.
std::uint64_t count(const std::string& regs);

// Merges `src` registers into `dst` (element-wise max).
void merge(std::string& dst, const std::string& src);

}  // namespace hll
}  // namespace blazekv

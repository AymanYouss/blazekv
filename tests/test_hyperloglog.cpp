#include "blazekv/hyperloglog.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace blazekv;

TEST(HyperLogLog, CountsDistinctWithinError) {
    std::string regs;
    constexpr int kN = 100000;
    for (int i = 0; i < kN; ++i) hll::add(regs, "element:" + std::to_string(i));
    std::uint64_t est = hll::count(regs);
    double err = std::abs(static_cast<double>(est) - kN) / kN;
    // Standard error at precision 14 is ~0.81%; allow a comfortable 3% bound.
    EXPECT_LT(err, 0.03) << "estimate=" << est;
}

TEST(HyperLogLog, DuplicatesDoNotInflate) {
    std::string regs;
    for (int r = 0; r < 100; ++r)
        for (int i = 0; i < 1000; ++i) hll::add(regs, "x:" + std::to_string(i));
    std::uint64_t est = hll::count(regs);
    double err = std::abs(static_cast<double>(est) - 1000) / 1000;
    EXPECT_LT(err, 0.05) << "estimate=" << est;
}

TEST(HyperLogLog, Merge) {
    std::string a, b;
    for (int i = 0; i < 5000; ++i) hll::add(a, "a" + std::to_string(i));
    for (int i = 0; i < 5000; ++i) hll::add(b, "b" + std::to_string(i));
    hll::merge(a, b);
    std::uint64_t est = hll::count(a);
    double err = std::abs(static_cast<double>(est) - 10000) / 10000;
    EXPECT_LT(err, 0.03) << "estimate=" << est;
}

TEST(HyperLogLog, SmallCardinalityLinearCounting) {
    std::string regs;
    for (int i = 0; i < 10; ++i) hll::add(regs, "item" + std::to_string(i));
    std::uint64_t est = hll::count(regs);
    EXPECT_GE(est, 9u);
    EXPECT_LE(est, 11u);
}

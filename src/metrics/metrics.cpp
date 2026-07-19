#include "blazekv/metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace blazekv {
namespace {

// Geometric bucket upper bounds in microseconds, spanning 1us .. ~60s. Computed
// once; shared by every histogram instance.
const std::array<double, LatencyHistogram::kBuckets + 1>& bounds() {
    static const std::array<double, LatencyHistogram::kBuckets + 1> b = [] {
        std::array<double, LatencyHistogram::kBuckets + 1> a{};
        const double lo = 1.0;             // 1 microsecond
        const double hi = 60.0 * 1e6;      // 60 seconds
        const double factor = std::pow(hi / lo, 1.0 / LatencyHistogram::kBuckets);
        double v = lo;
        for (int i = 0; i <= LatencyHistogram::kBuckets; ++i) {
            a[i] = v;
            v *= factor;
        }
        return a;
    }();
    return b;
}

}  // namespace

LatencyHistogram::LatencyHistogram() {
    for (auto& c : counts_) c.store(0, std::memory_order_relaxed);
}

int LatencyHistogram::bucket_for(std::uint64_t micros) {
    const auto& b = bounds();
    // upper_bound returns first bound strictly greater than the value.
    auto it = std::upper_bound(b.begin(), b.end() - 1, static_cast<double>(micros));
    int idx = static_cast<int>(it - b.begin());
    if (idx >= kBuckets) idx = kBuckets - 1;
    return idx;
}

void LatencyHistogram::record(std::uint64_t micros) {
    counts_[bucket_for(micros)].fetch_add(1, std::memory_order_relaxed);
    total_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(micros, std::memory_order_relaxed);
    std::uint64_t cur = max_.load(std::memory_order_relaxed);
    while (micros > cur &&
           !max_.compare_exchange_weak(cur, micros, std::memory_order_relaxed)) {
    }
}

std::uint64_t LatencyHistogram::percentile(double p) const {
    const std::uint64_t total = total_.load(std::memory_order_relaxed);
    if (total == 0) return 0;
    const std::uint64_t target = static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(total)));
    std::uint64_t cumulative = 0;
    const auto& b = bounds();
    for (int i = 0; i < kBuckets; ++i) {
        cumulative += counts_[i].load(std::memory_order_relaxed);
        if (cumulative >= target) return static_cast<std::uint64_t>(b[i + 1]);
    }
    return max_.load(std::memory_order_relaxed);
}

double LatencyHistogram::mean_us() const {
    const std::uint64_t total = total_.load(std::memory_order_relaxed);
    if (total == 0) return 0.0;
    return static_cast<double>(sum_.load(std::memory_order_relaxed)) / static_cast<double>(total);
}

double LatencyHistogram::bucket_bound_seconds(int i) {
    if (i < 0) i = 0;
    if (i > kBuckets) i = kBuckets;
    return bounds()[i] / 1e6;
}

void LatencyHistogram::reset() {
    for (auto& c : counts_) c.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    sum_.store(0, std::memory_order_relaxed);
    max_.store(0, std::memory_order_relaxed);
}

}  // namespace blazekv

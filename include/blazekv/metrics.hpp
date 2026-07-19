#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace blazekv {

// A log-scale latency histogram (microsecond resolution). Buckets are geometric
// so a fixed number of them cover 1us .. ~60s at a bounded relative error, which
// is what makes p99/p999 cheap to compute for the dashboard and /metrics.
class LatencyHistogram {
   public:
    static constexpr int kBuckets = 512;

    LatencyHistogram();
    void record(std::uint64_t micros);
    // Estimated percentile in microseconds (p in [0,1]).
    std::uint64_t percentile(double p) const;
    double mean_us() const;
    std::uint64_t count() const { return total_.load(std::memory_order_relaxed); }
    std::uint64_t max_us() const { return max_.load(std::memory_order_relaxed); }
    // Prometheus histogram bucket boundary (upper bound, seconds) for bucket i.
    static double bucket_bound_seconds(int i);
    std::uint64_t bucket_count(int i) const { return counts_[i].load(std::memory_order_relaxed); }
    void reset();

   private:
    static int bucket_for(std::uint64_t micros);
    std::atomic<std::uint64_t> counts_[kBuckets];
    std::atomic<std::uint64_t> total_{0};
    std::atomic<std::uint64_t> sum_{0};
    std::atomic<std::uint64_t> max_{0};
};

// Counters owned by a single shard. Only that shard's thread writes them, so the
// atomics use relaxed ordering purely to let the metrics thread read them safely.
struct ShardMetrics {
    std::atomic<std::uint64_t> commands_total{0};
    std::atomic<std::uint64_t> reads{0};
    std::atomic<std::uint64_t> writes{0};
    std::atomic<std::uint64_t> keyspace_hits{0};
    std::atomic<std::uint64_t> keyspace_misses{0};
    std::atomic<std::uint64_t> expired_keys{0};
    std::atomic<std::uint64_t> connections_open{0};
    std::atomic<std::uint64_t> connections_total{0};
    std::atomic<std::uint64_t> net_input_bytes{0};
    std::atomic<std::uint64_t> net_output_bytes{0};
    std::atomic<std::uint64_t> cross_shard_ops{0};
    std::atomic<std::uint64_t> keys{0};
    std::atomic<std::uint64_t> expires{0};
    std::atomic<std::uint64_t> memory_bytes{0};
    LatencyHistogram latency;

    void observe(std::uint64_t micros) {
        commands_total.fetch_add(1, std::memory_order_relaxed);
        latency.record(micros);
    }
};

}  // namespace blazekv

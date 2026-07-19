#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "blazekv/config.hpp"
#include "blazekv/hash.hpp"

namespace blazekv {

class Shard;

// The top-level server: owns the shards, the routing function, lifecycle, and the
// observability HTTP endpoint. There is no global data structure guarded by a
// lock; the server only coordinates startup, shutdown, and snapshotting.
class Server {
   public:
    explicit Server(Config cfg);
    ~Server();

    // Binds listeners, loads persistence, and starts every shard thread.
    bool start(std::string& err);
    // Blocks until stop() is called (e.g. by a signal handler).
    void run_until_signal();
    void stop();

    const Config& config() const { return cfg_; }
    unsigned shard_count() const { return static_cast<unsigned>(shards_.size()); }
    Shard& shard(unsigned i) { return *shards_[i]; }

    // Maps a key to its owning shard. This is the single source of truth for
    // partitioning, used identically on every core so routing is consistent.
    unsigned shard_for(std::string_view key) const {
        return static_cast<unsigned>(hash_bytes(key) % shards_.size());
    }

    std::uint64_t start_time_ms() const { return start_time_ms_; }

    // Orchestrates a fork()-based point-in-time snapshot of every shard. The child
    // serializes copy-on-write memory while the shards keep serving.
    bool trigger_snapshot(std::string& err);

    // Aggregated metrics / INFO, rendered on demand for /metrics and the dashboard.
    std::string render_prometheus() const;
    std::string render_info(std::string_view section) const;
    std::string render_dashboard_json() const;

   private:
    void run_metrics_server();

    Config cfg_;
    std::vector<std::unique_ptr<Shard>> shards_;
    std::vector<int> listen_fds_;
    std::uint64_t start_time_ms_ = 0;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> metrics_thread_;
};

}  // namespace blazekv

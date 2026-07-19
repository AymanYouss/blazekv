#pragma once

#include <cstdint>
#include <string>

namespace blazekv {

enum class FsyncPolicy { Always, EverySec, No };
enum class ReactorKind { Auto, Uring, Epoll, Poll };

// Server configuration, populated from CLI flags, a redis.conf-style file, or the
// CONFIG command. Mirrors the subset of Redis directives that matter here.
struct Config {
    std::string bind = "0.0.0.0";
    std::uint16_t port = 6380;
    unsigned shards = 0;  // 0 => one shard per hardware core
    bool pin_threads = true;

    // Persistence
    bool aof_enabled = true;
    std::string aof_path = "blazekv.aof";
    FsyncPolicy fsync = FsyncPolicy::EverySec;
    bool snapshot_enabled = true;
    std::string snapshot_path = "blazekv.blaze";
    std::string dir = ".";

    // Memory / eviction
    std::uint64_t maxmemory = 0;  // 0 => unlimited
    unsigned active_expire_hz = 10;

    // Networking
    int tcp_backlog = 1024;
    bool tcp_nodelay = true;
    ReactorKind reactor = ReactorKind::Auto;

    // Observability
    std::uint16_t metrics_port = 9121;  // Prometheus /metrics + dashboard API
    bool metrics_enabled = true;

    static Config from_args(int argc, char** argv);
    bool load_file(const std::string& path, std::string& err);
};

}  // namespace blazekv

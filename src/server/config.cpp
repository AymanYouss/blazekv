#include "blazekv/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace blazekv {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool truthy(const std::string& v) {
    std::string l = lower(v);
    return l == "yes" || l == "true" || l == "1" || l == "on";
}

FsyncPolicy parse_fsync(const std::string& v) {
    std::string l = lower(v);
    if (l == "always") return FsyncPolicy::Always;
    if (l == "no") return FsyncPolicy::No;
    return FsyncPolicy::EverySec;
}

ReactorKind parse_reactor(const std::string& v) {
    std::string l = lower(v);
    if (l == "uring" || l == "io_uring") return ReactorKind::Uring;
    if (l == "epoll") return ReactorKind::Epoll;
    if (l == "poll") return ReactorKind::Poll;
    return ReactorKind::Auto;
}

// Applies a single directive to the config. Accepts redis.conf-style keys.
void apply(Config& cfg, const std::string& key, const std::string& value) {
    std::string k = lower(key);
    if (k == "bind") cfg.bind = value;
    else if (k == "port") cfg.port = static_cast<std::uint16_t>(std::stoi(value));
    else if (k == "shards" || k == "io-threads") cfg.shards = static_cast<unsigned>(std::stoul(value));
    else if (k == "pin-threads") cfg.pin_threads = truthy(value);
    else if (k == "appendonly") cfg.aof_enabled = truthy(value);
    else if (k == "appendfilename") cfg.aof_path = value;
    else if (k == "appendfsync") cfg.fsync = parse_fsync(value);
    else if (k == "save" || k == "snapshot") cfg.snapshot_enabled = truthy(value) || !value.empty();
    else if (k == "dbfilename" || k == "snapshotfile") cfg.snapshot_path = value;
    else if (k == "dir") cfg.dir = value;
    else if (k == "maxmemory") cfg.maxmemory = std::strtoull(value.c_str(), nullptr, 10);
    else if (k == "hz" || k == "active-expire-hz") cfg.active_expire_hz = static_cast<unsigned>(std::stoul(value));
    else if (k == "tcp-backlog") cfg.tcp_backlog = std::stoi(value);
    else if (k == "tcp-nodelay") cfg.tcp_nodelay = truthy(value);
    else if (k == "reactor") cfg.reactor = parse_reactor(value);
    else if (k == "metrics-port") cfg.metrics_port = static_cast<std::uint16_t>(std::stoi(value));
    else if (k == "metrics-enabled") cfg.metrics_enabled = truthy(value);
}

}  // namespace

bool Config::load_file(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open config file: " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        auto hash = trimmed.find('#');
        if (hash != std::string::npos) trimmed = trimmed.substr(0, hash);
        std::istringstream iss(trimmed);
        std::string key, value, rest;
        if (!(iss >> key)) continue;
        std::getline(iss, rest);
        // Trim leading whitespace from the value remainder.
        std::size_t start = rest.find_first_not_of(" \t");
        value = start == std::string::npos ? "" : rest.substr(start);
        apply(*this, key, value);
    }
    return true;
}

Config Config::from_args(int argc, char** argv) {
    Config cfg;
    std::string err;
    int i = 1;
    // An optional leading positional argument is a config file path.
    if (argc > 1 && argv[1][0] != '-') {
        cfg.load_file(argv[1], err);
        i = 2;
    }
    for (; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-') {
            std::string key = arg.substr(2);
            std::string value = (i + 1 < argc) ? argv[++i] : "";
            apply(cfg, key, value);
        }
    }
    return cfg;
}

}  // namespace blazekv

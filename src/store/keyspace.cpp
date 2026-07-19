#include "blazekv/keyspace.hpp"

namespace blazekv {

std::size_t Keyspace::active_expire_cycle(std::size_t budget,
                                          std::vector<std::string>& expired_out) {
    if (expires_.empty()) return 0;
    const std::uint64_t now = now_ms();
    std::vector<std::string> dead;
    std::size_t seen = 0;
    // Sample TTL-bearing keys and collect the ones already past their deadline.
    // Iteration is over the shard's own tables, so no synchronization is needed.
    expires_.for_each([&](const std::string& key, std::uint64_t& deadline) {
        if (seen >= budget) return;
        ++seen;
        if (deadline <= now) dead.push_back(key);
    });
    for (const auto& k : dead) {
        dict_.erase(k);
        expires_.erase(k);
        ++stats_expired_;
        expired_out.push_back(k);
    }
    return dead.size();
}

}  // namespace blazekv

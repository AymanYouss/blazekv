#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "blazekv/object.hpp"
#include "blazekv/swiss_table.hpp"

namespace blazekv {

// The data owned by a single shard: a key->Object dictionary plus a parallel
// key->deadline dictionary for TTLs. Expiration is both lazy (checked on access)
// and active (a periodic sampling cycle driven by the shard's event loop),
// matching Redis semantics while staying entirely shard-local and lock-free.
class Keyspace {
   public:
    // Returns the live object for `key`, transparently deleting it first if its TTL
    // has elapsed (lazy expiration). Returns nullptr if absent or expired.
    Object* lookup(std::string_view key) {
        Object* obj = dict_.find(key);
        if (obj == nullptr) return nullptr;
        if (BLAZEKV_UNLIKELY(!expires_.empty())) {
            if (const std::uint64_t* dl = expires_.find(key)) {
                if (*dl <= now_ms()) {
                    erase(key);
                    ++stats_expired_;
                    return nullptr;
                }
            }
        }
        return obj;
    }

    bool exists(std::string_view key) { return lookup(key) != nullptr; }

    // Inserts or overwrites the value at `key`, clearing any prior TTL.
    Object& set(std::string_view key, Object value) {
        std::string k(key);
        expires_.erase(k);
        auto [slot, inserted] = dict_.insert_or_assign(std::move(k), std::move(value));
        return *slot;
    }

    // Returns the object at `key`, creating it via `factory` if absent. Does not
    // touch TTL of an existing key.
    template <class Factory>
    Object& get_or_create(std::string_view key, Factory&& factory) {
        if (Object* obj = lookup(key)) return *obj;
        auto [slot, inserted] =
            dict_.insert_or_assign(std::string(key), std::forward<Factory>(factory)());
        return *slot;
    }

    bool erase(std::string_view key) {
        std::string k(key);
        expires_.erase(k);
        return dict_.erase(k);
    }

    // TTL management (absolute deadlines in wall-clock ms).
    void set_deadline(std::string_view key, std::uint64_t deadline_ms) {
        expires_.insert_or_assign(std::string(key), deadline_ms);
    }
    bool persist(std::string_view key) { return expires_.erase(std::string(key)); }
    // -1 == no expire, -2 == key missing, else remaining ms.
    std::int64_t ttl_ms(std::string_view key) {
        if (!exists(key)) return -2;
        const std::uint64_t* dl = expires_.find(key);
        if (dl == nullptr) return -1;
        const std::uint64_t now = now_ms();
        return *dl > now ? static_cast<std::int64_t>(*dl - now) : 0;
    }
    bool has_expire(std::string_view key) { return expires_.find(key) != nullptr; }
    // Absolute deadline in ms, or 0 if the key has no TTL. Used by snapshotting.
    std::uint64_t deadline_or_zero(std::string_view key) {
        const std::uint64_t* dl = expires_.find(key);
        return dl ? *dl : 0;
    }

    std::size_t size() const { return dict_.size(); }
    std::size_t expires_size() const { return expires_.size(); }
    std::uint64_t expired_total() const { return stats_expired_; }

    void clear() {
        dict_.clear();
        expires_.clear();
    }

    template <class Fn>
    void for_each(Fn&& fn) {
        dict_.for_each(std::forward<Fn>(fn));
    }

    // Samples up to `budget` keys with TTLs and evicts those already dead. Returns
    // the number expired this cycle; the caller propagates DEL to AOF/replicas.
    std::size_t active_expire_cycle(std::size_t budget, std::vector<std::string>& expired_out);

    std::size_t approx_memory() const { return dict_.memory_bytes() + expires_.memory_bytes(); }

   private:
    SwissTable<std::string, Object> dict_;
    SwissTable<std::string, std::uint64_t> expires_;
    std::uint64_t stats_expired_ = 0;
};

}  // namespace blazekv

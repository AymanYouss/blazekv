#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "blazekv/build_config.hpp"
#include "blazekv/common.hpp"
#include "blazekv/hash.hpp"

#if BLAZEKV_HAVE_SSE2
#include <emmintrin.h>
#elif BLAZEKV_HAVE_NEON
#include <arm_neon.h>
#endif

namespace blazekv {

// An open-addressing hash table using the "Swiss table" / F14 design: control
// bytes store the low 7 bits of each slot's hash so that a full 16-slot group can
// be probed in parallel with a single SIMD compare. This keeps probing branchless
// and cache friendly, which is exactly the property that makes GET/SET fast.
//
// Metadata byte layout (per Abseil):
//   0x80  -> empty
//   0xFE  -> deleted (tombstone)
//   0h7   -> full, holds low 7 bits of the hash (top bit 0)
template <class Key, class Value, class Hash = BytesHash, class KeyEq = std::equal_to<>>
class SwissTable {
    static constexpr std::int8_t kEmpty = static_cast<std::int8_t>(0x80);
    static constexpr std::int8_t kDeleted = static_cast<std::int8_t>(0xFE);
    static constexpr std::size_t kGroup = 16;

   public:
    struct Slot {
        Key key;
        Value value;
    };

    SwissTable() { reserve(8); }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // Memory footprint of the table's control + slot arrays (excludes value heap).
    [[nodiscard]] std::size_t memory_bytes() const noexcept {
        return capacity_ * (sizeof(Slot) + 1) + kGroup;
    }

    template <class K>
    Value* find(const K& key) {
        const std::uint64_t h = hasher_(key);
        std::size_t pos = h1(h) & mask_;
        const std::int8_t needle = h2(h);
        while (true) {
            const std::int8_t* group = ctrl_ + pos;
            auto match = MatchBits(group, needle);
            while (match) {
                const std::size_t i = (pos + ctz(match)) & mask_;
                if (BLAZEKV_LIKELY(eq_(slots_[i].key, key))) return &slots_[i].value;
                match &= match - 1;
            }
            if (BLAZEKV_LIKELY(MatchEmpty(group))) return nullptr;
            pos = (pos + kGroup) & mask_;
        }
    }

    template <class K>
    const Value* find(const K& key) const {
        return const_cast<SwissTable*>(this)->find(key);
    }

    // Inserts or overwrites; returns {value*, inserted?}.
    template <class K, class V>
    std::pair<Value*, bool> insert_or_assign(K&& key, V&& value) {
        if (BLAZEKV_UNLIKELY(size_ + 1 > grow_at_)) rehash(capacity_ * 2);
        const std::uint64_t h = hasher_(key);
        std::size_t pos = h1(h) & mask_;
        const std::int8_t needle = h2(h);
        std::size_t insert_at = kInvalid;
        while (true) {
            const std::int8_t* group = ctrl_ + pos;
            auto match = MatchBits(group, needle);
            while (match) {
                const std::size_t i = (pos + ctz(match)) & mask_;
                if (eq_(slots_[i].key, key)) {
                    slots_[i].value = std::forward<V>(value);
                    return {&slots_[i].value, false};
                }
                match &= match - 1;
            }
            auto empties = MatchEmptyOrDeleted(group);
            if (empties) {
                if (insert_at == kInvalid) insert_at = (pos + ctz(empties)) & mask_;
                if (MatchEmpty(group)) break;
            }
            pos = (pos + kGroup) & mask_;
        }
        set_ctrl(insert_at, needle);
        slots_[insert_at].key = std::forward<K>(key);
        slots_[insert_at].value = std::forward<V>(value);
        ++size_;
        return {&slots_[insert_at].value, true};
    }

    template <class K>
    bool erase(const K& key) {
        const std::uint64_t h = hasher_(key);
        std::size_t pos = h1(h) & mask_;
        const std::int8_t needle = h2(h);
        while (true) {
            const std::int8_t* group = ctrl_ + pos;
            auto match = MatchBits(group, needle);
            while (match) {
                const std::size_t i = (pos + ctz(match)) & mask_;
                if (eq_(slots_[i].key, key)) {
                    // A tombstone is only required if the group has no empty slot;
                    // otherwise we can mark empty and reclaim the probe budget.
                    if (MatchEmpty(group)) {
                        set_ctrl(i, kEmpty);
                    } else {
                        set_ctrl(i, kDeleted);
                    }
                    slots_[i].key = Key{};
                    slots_[i].value = Value{};
                    --size_;
                    return true;
                }
                match &= match - 1;
            }
            if (MatchEmpty(group)) return false;
            pos = (pos + kGroup) & mask_;
        }
    }

    template <class Fn>
    void for_each(Fn&& fn) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            if (ctrl_[i] >= 0) fn(slots_[i].key, slots_[i].value);
        }
    }

    void reserve(std::size_t n) {
        std::size_t need = 8;
        while (need * 7 / 8 < n) need *= 2;
        if (need > capacity_) rehash(need);
    }

    void clear() {
        std::memset(ctrl_, kEmpty, capacity_ + kGroup);
        for (std::size_t i = 0; i < capacity_; ++i) slots_[i] = Slot{};
        size_ = 0;
    }

   private:
    static constexpr std::size_t kInvalid = ~std::size_t{0};

    static std::uint64_t h1(std::uint64_t h) noexcept { return h >> 7; }
    static std::int8_t h2(std::uint64_t h) noexcept {
        return static_cast<std::int8_t>(h & 0x7F);
    }

    void set_ctrl(std::size_t i, std::int8_t v) noexcept {
        ctrl_[i] = v;
        // Mirror the first kGroup control bytes at the tail so a group read that
        // starts near the end of the array never walks out of bounds.
        if (i < kGroup) ctrl_[capacity_ + i] = v;
    }

#if BLAZEKV_HAVE_SSE2
    static std::uint32_t MatchBits(const std::int8_t* g, std::int8_t needle) {
        __m128i ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(g));
        __m128i cmp = _mm_cmpeq_epi8(_mm_set1_epi8(needle), ctrl);
        return static_cast<std::uint32_t>(_mm_movemask_epi8(cmp));
    }
    static bool MatchEmpty(const std::int8_t* g) {
        __m128i ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(g));
        __m128i cmp = _mm_cmpeq_epi8(_mm_set1_epi8(kEmpty), ctrl);
        return _mm_movemask_epi8(cmp) != 0;
    }
    static std::uint32_t MatchEmptyOrDeleted(const std::int8_t* g) {
        // empty (0x80) and deleted (0xFE) both have the high bit set; full does not.
        __m128i ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(g));
        return static_cast<std::uint32_t>(_mm_movemask_epi8(ctrl));
    }
    static std::size_t ctz(std::uint32_t x) { return static_cast<std::size_t>(__builtin_ctz(x)); }
#elif BLAZEKV_HAVE_NEON
    static std::uint64_t MatchBits(const std::int8_t* g, std::int8_t needle) {
        uint8x16_t ctrl = vld1q_u8(reinterpret_cast<const std::uint8_t*>(g));
        uint8x16_t cmp = vceqq_u8(ctrl, vdupq_n_u8(static_cast<std::uint8_t>(needle)));
        return neon_mask(cmp);
    }
    static bool MatchEmpty(const std::int8_t* g) {
        uint8x16_t ctrl = vld1q_u8(reinterpret_cast<const std::uint8_t*>(g));
        uint8x16_t cmp = vceqq_u8(ctrl, vdupq_n_u8(0x80));
        return neon_mask(cmp) != 0;
    }
    static std::uint64_t MatchEmptyOrDeleted(const std::int8_t* g) {
        uint8x16_t ctrl = vld1q_u8(reinterpret_cast<const std::uint8_t*>(g));
        uint8x16_t cmp = vcgeq_u8(ctrl, vdupq_n_u8(0x80));  // high bit set
        return neon_mask(cmp);
    }
    // Compress a 16-byte compare result into a 4-bit-per-lane mask; ctz/4 yields
    // the matching lane index.
    static std::uint64_t neon_mask(uint8x16_t cmp) {
        uint8x8_t narrowed = vshrn_n_u16(vreinterpretq_u16_u8(cmp), 4);
        return vget_lane_u64(vreinterpret_u64_u8(narrowed), 0);
    }
    static std::size_t ctz(std::uint64_t x) {
        return static_cast<std::size_t>(__builtin_ctzll(x)) >> 2;
    }
#else
    static std::uint32_t MatchBits(const std::int8_t* g, std::int8_t needle) {
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < kGroup; ++i)
            if (g[i] == needle) m |= (1u << i);
        return m;
    }
    static bool MatchEmpty(const std::int8_t* g) {
        for (std::size_t i = 0; i < kGroup; ++i)
            if (g[i] == kEmpty) return true;
        return false;
    }
    static std::uint32_t MatchEmptyOrDeleted(const std::int8_t* g) {
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < kGroup; ++i)
            if (g[i] < 0) m |= (1u << i);
        return m;
    }
    static std::size_t ctz(std::uint32_t x) { return static_cast<std::size_t>(__builtin_ctz(x)); }
#endif

    void rehash(std::size_t new_cap) {
        std::vector<std::int8_t> old_ctrl = std::move(ctrl_store_);
        std::vector<Slot> old_slots = std::move(slots_store_);
        const std::size_t old_cap = capacity_;

        capacity_ = new_cap;
        mask_ = new_cap - 1;
        grow_at_ = new_cap * 7 / 8;
        ctrl_store_.assign(new_cap + kGroup, kEmpty);
        // Value-construct rather than assign so move-only values (e.g. Object,
        // which owns a unique_ptr) are supported.
        slots_store_ = std::vector<Slot>(new_cap);
        ctrl_ = ctrl_store_.data();
        slots_ = slots_store_.data();
        size_ = 0;

        for (std::size_t i = 0; i < old_cap; ++i) {
            if (old_ctrl[i] >= 0) {
                insert_moved(std::move(old_slots[i].key), std::move(old_slots[i].value));
            }
        }
    }

    void insert_moved(Key&& key, Value&& value) {
        const std::uint64_t h = hasher_(key);
        std::size_t pos = h1(h) & mask_;
        const std::int8_t needle = h2(h);
        while (true) {
            const std::int8_t* group = ctrl_ + pos;
            auto empties = MatchEmptyOrDeleted(group);
            if (empties) {
                const std::size_t i = (pos + ctz(empties)) & mask_;
                set_ctrl(i, needle);
                slots_[i].key = std::move(key);
                slots_[i].value = std::move(value);
                ++size_;
                return;
            }
            pos = (pos + kGroup) & mask_;
        }
    }

    std::vector<std::int8_t> ctrl_store_;
    std::vector<Slot> slots_store_;
    std::int8_t* ctrl_ = nullptr;
    Slot* slots_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::size_t size_ = 0;
    std::size_t grow_at_ = 0;
    Hash hasher_{};
    KeyEq eq_{};
};

}  // namespace blazekv

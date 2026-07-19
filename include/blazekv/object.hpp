#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <variant>

#include "blazekv/common.hpp"
#include "blazekv/skiplist.hpp"
#include "blazekv/swiss_table.hpp"

namespace blazekv {

enum class ObjType : std::uint8_t {
    String = 0,
    List = 1,
    Hash = 2,
    Set = 3,
    ZSet = 4,
    HLL = 5,
};

const char* type_name(ObjType t) noexcept;

using ListType = std::deque<std::string>;
using HashType = SwissTable<std::string, std::string>;
using SetType = SwissTable<std::string, std::uint8_t>;

// Sorted set: a member->score dictionary for O(1) score lookup plus a skip list
// ordered by (score, member) for O(log N) range and rank queries.
struct ZSetType {
    SwissTable<std::string, double> dict;
    SkipList sl;

    std::size_t size() const { return dict.size(); }

    // Returns true if the member was newly added (false = score updated).
    bool set(const std::string& member, double score) {
        if (double* cur = dict.find(member)) {
            if (*cur == score) return false;
            sl.erase(member, *cur);
            sl.insert(member, score);
            *cur = score;
            return false;
        }
        dict.insert_or_assign(member, score);
        sl.insert(member, score);
        return true;
    }

    bool remove(const std::string& member) {
        if (double* cur = dict.find(member)) {
            sl.erase(member, *cur);
            dict.erase(member);
            return true;
        }
        return false;
    }
};

// A value object. Complex containers are heap-allocated behind a unique_ptr so the
// Object itself stays small (tag + pointer) and cheap to move between shards.
class Object {
   public:
    Object() : type_(ObjType::String), data_(std::string{}) {}
    explicit Object(std::string s) : type_(ObjType::String), data_(std::move(s)) {}

    static Object make_list() { return Object(ObjType::List, std::make_unique<ListType>()); }
    static Object make_hash() { return Object(ObjType::Hash, std::make_unique<HashType>()); }
    static Object make_set() { return Object(ObjType::Set, std::make_unique<SetType>()); }
    static Object make_zset() { return Object(ObjType::ZSet, std::make_unique<ZSetType>()); }
    static Object make_hll() {
        Object o;
        o.type_ = ObjType::HLL;
        return o;
    }

    ObjType type() const noexcept { return type_; }

    std::string& str() { return std::get<std::string>(data_); }
    const std::string& str() const { return std::get<std::string>(data_); }
    ListType& list() { return *std::get<std::unique_ptr<ListType>>(data_); }
    HashType& hash() { return *std::get<std::unique_ptr<HashType>>(data_); }
    SetType& set() { return *std::get<std::unique_ptr<SetType>>(data_); }
    ZSetType& zset() { return *std::get<std::unique_ptr<ZSetType>>(data_); }
    const ZSetType& zset() const { return *std::get<std::unique_ptr<ZSetType>>(data_); }

    // Approximate heap footprint, used by INFO memory and eviction accounting.
    std::size_t memory_usage() const;

   private:
    Object(ObjType t, std::unique_ptr<ListType> p) : type_(t), data_(std::move(p)) {}
    Object(ObjType t, std::unique_ptr<HashType> p) : type_(t), data_(std::move(p)) {}
    Object(ObjType t, std::unique_ptr<SetType> p) : type_(t), data_(std::move(p)) {}
    Object(ObjType t, std::unique_ptr<ZSetType> p) : type_(t), data_(std::move(p)) {}

    ObjType type_;
    std::variant<std::string, std::unique_ptr<ListType>, std::unique_ptr<HashType>,
                 std::unique_ptr<SetType>, std::unique_ptr<ZSetType>>
        data_;
};

// String encoding helpers (Redis stores integers specially; we parse on demand).
bool parse_int(std::string_view s, std::int64_t& out) noexcept;
bool parse_double(std::string_view s, double& out) noexcept;
std::string int_to_string(std::int64_t v);
std::string double_to_string(double v);

}  // namespace blazekv

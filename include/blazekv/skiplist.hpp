#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "blazekv/common.hpp"

namespace blazekv {

// An indexable skip list ordered by (score, member), mirroring Redis' zset design.
// Alongside the skip list a dictionary (member -> score) lives in the caller (the
// ZSet object), so point lookups are O(1) and range/rank queries are O(log N).
// Each level carries a span so ZRANK / ZRANGE-by-rank are also O(log N).
class SkipList {
   public:
    static constexpr int kMaxLevel = 32;
    static constexpr double kP = 0.25;

    struct Node {
        std::string member;
        double score;
        struct Level {
            Node* forward = nullptr;
            std::uint32_t span = 0;
        };
        Node* backward = nullptr;
        std::vector<Level> level;
        Node(std::string m, double s, int lvl) : member(std::move(m)), score(s), level(lvl) {}
    };

    SkipList() : head_(new Node(std::string(), 0, kMaxLevel)), rng_(0x1234ABCD) {}
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    ~SkipList() {
        Node* n = head_->level[0].forward;
        delete head_;
        while (n) {
            Node* next = n->level[0].forward;
            delete n;
            n = next;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return length_; }

    // Inserts a (score, member). Caller guarantees the member is not already present.
    Node* insert(std::string member, double score) {
        Node* update[kMaxLevel];
        std::uint32_t rank[kMaxLevel];
        Node* x = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            rank[i] = i == level_ - 1 ? 0 : rank[i + 1];
            while (x->level[i].forward &&
                   (x->level[i].forward->score < score ||
                    (x->level[i].forward->score == score && x->level[i].forward->member < member))) {
                rank[i] += x->level[i].span;
                x = x->level[i].forward;
            }
            update[i] = x;
        }
        const int lvl = random_level();
        if (lvl > level_) {
            for (int i = level_; i < lvl; ++i) {
                rank[i] = 0;
                update[i] = head_;
                update[i]->level[i].span = static_cast<std::uint32_t>(length_);
            }
            level_ = lvl;
        }
        x = new Node(std::move(member), score, lvl);
        for (int i = 0; i < lvl; ++i) {
            x->level[i].forward = update[i]->level[i].forward;
            update[i]->level[i].forward = x;
            x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
            update[i]->level[i].span = (rank[0] - rank[i]) + 1;
        }
        for (int i = lvl; i < level_; ++i) update[i]->level[i].span++;
        x->backward = (update[0] == head_) ? nullptr : update[0];
        if (x->level[0].forward)
            x->level[0].forward->backward = x;
        else
            tail_ = x;
        ++length_;
        return x;
    }

    bool erase(std::string_view member, double score) {
        Node* update[kMaxLevel];
        Node* x = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->level[i].forward &&
                   (x->level[i].forward->score < score ||
                    (x->level[i].forward->score == score && x->level[i].forward->member < member))) {
                x = x->level[i].forward;
            }
            update[i] = x;
        }
        x = x->level[0].forward;
        if (x && x->score == score && x->member == member) {
            remove_node(x, update);
            delete x;
            return true;
        }
        return false;
    }

    // 1-based rank of (score, member), or 0 if absent.
    [[nodiscard]] std::uint32_t rank_of(std::string_view member, double score) const {
        Node* x = head_;
        std::uint32_t rank = 0;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->level[i].forward &&
                   (x->level[i].forward->score < score ||
                    (x->level[i].forward->score == score &&
                     x->level[i].forward->member <= member))) {
                rank += x->level[i].span;
                x = x->level[i].forward;
            }
            if (x != head_ && x->score == score && x->member == member) return rank;
        }
        return 0;
    }

    // Node at 1-based rank, or nullptr.
    [[nodiscard]] Node* at_rank(std::uint32_t rank) const {
        Node* x = head_;
        std::uint32_t traversed = 0;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->level[i].forward && traversed + x->level[i].span <= rank) {
                traversed += x->level[i].span;
                x = x->level[i].forward;
            }
            if (traversed == rank && x != head_) return x;
        }
        return nullptr;
    }

    // First node with score >= min (inclusive lower bound for range scans).
    [[nodiscard]] Node* first_gte(double min) const {
        Node* x = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->level[i].forward && x->level[i].forward->score < min) x = x->level[i].forward;
        }
        return x->level[0].forward;
    }

    [[nodiscard]] Node* head() const { return head_->level[0].forward; }
    [[nodiscard]] Node* tail() const { return tail_; }

   private:
    int random_level() {
        int lvl = 1;
        while (lvl < kMaxLevel && (rng_() & 0xFFFF) < static_cast<std::uint32_t>(kP * 0xFFFF))
            ++lvl;
        return lvl;
    }

    void remove_node(Node* x, Node** update) {
        for (int i = 0; i < level_; ++i) {
            if (update[i]->level[i].forward == x) {
                update[i]->level[i].span += x->level[i].span - 1;
                update[i]->level[i].forward = x->level[i].forward;
            } else {
                update[i]->level[i].span -= 1;
            }
        }
        if (x->level[0].forward)
            x->level[0].forward->backward = x->backward;
        else
            tail_ = x->backward;
        while (level_ > 1 && head_->level[level_ - 1].forward == nullptr) --level_;
        --length_;
    }

    Node* head_;
    Node* tail_ = nullptr;
    std::size_t length_ = 0;
    int level_ = 1;
    std::mt19937 rng_;
};

}  // namespace blazekv

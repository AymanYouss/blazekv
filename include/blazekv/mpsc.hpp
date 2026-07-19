#pragma once

#include <atomic>
#include <utility>

#include "blazekv/common.hpp"

namespace blazekv {

// A multi-producer, single-consumer intrusive queue.
//
// Any shard may enqueue a task destined for another shard from its own thread
// with a single atomic exchange (wait-free for producers). The owning shard
// drains the whole batch at once and reverses it to restore FIFO order, which is
// lock-free and touches each node exactly once. This is the only place two cores
// share memory: single-shard commands execute inline and never reach this queue,
// so the shared-nothing hot path stays free of contention.
template <class T>
class MpscQueue {
   public:
    MpscQueue() = default;
    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;
    ~MpscQueue() {
        Node* n = head_.load(std::memory_order_relaxed);
        while (n) {
            Node* next = n->next;
            delete n;
            n = next;
        }
    }

    void push(T value) {
        Node* node = new Node{nullptr, std::move(value)};
        Node* head = head_.load(std::memory_order_relaxed);
        do {
            node->next = head;
        } while (!head_.compare_exchange_weak(head, node, std::memory_order_release,
                                              std::memory_order_relaxed));
    }

    // Consumer side (owning shard only). Invokes `fn(T&&)` on every queued item in
    // FIFO order and returns the number processed.
    template <class Fn>
    std::size_t drain(Fn&& fn) {
        Node* list = head_.exchange(nullptr, std::memory_order_acquire);
        if (list == nullptr) return 0;
        // The producer stack is LIFO; reverse it to recover enqueue order.
        Node* prev = nullptr;
        while (list) {
            Node* next = list->next;
            list->next = prev;
            prev = list;
            list = next;
        }
        std::size_t count = 0;
        while (prev) {
            Node* next = prev->next;
            fn(std::move(prev->value));
            delete prev;
            prev = next;
            ++count;
        }
        return count;
    }

    bool empty() const { return head_.load(std::memory_order_acquire) == nullptr; }

   private:
    struct Node {
        Node* next;
        T value;
    };
    alignas(kCacheLine) std::atomic<Node*> head_{nullptr};
};

}  // namespace blazekv

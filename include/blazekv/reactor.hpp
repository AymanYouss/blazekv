#pragma once

#include <cstdint>
#include <memory>

#include "blazekv/config.hpp"

namespace blazekv {

enum EventFlags : std::uint32_t {
    kRead = 1u << 0,
    kWrite = 1u << 1,
    kError = 1u << 2,
};

struct ReadyEvent {
    int fd;
    void* token;
    std::uint32_t flags;
};

// Event-loop backend abstraction. Concrete implementations: io_uring (Linux, the
// fast path), epoll (Linux fallback), and poll (portable, used on macOS and as a
// last resort). The shard drives exactly one Reactor on its own thread.
class Reactor {
   public:
    virtual ~Reactor() = default;
    virtual bool add(int fd, std::uint32_t flags, void* token) = 0;
    virtual bool mod(int fd, std::uint32_t flags, void* token) = 0;
    virtual bool del(int fd) = 0;
    // Waits up to `timeout_ms` (-1 = block) and writes ready events into `out`
    // (capacity `max`). Returns the number of events, or -1 on fatal error.
    virtual int wait(ReadyEvent* out, int max, int timeout_ms) = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<Reactor> make_reactor(ReactorKind kind);

// A cross-thread wakeup primitive (eventfd on Linux, self-pipe elsewhere). A
// remote shard calls notify() after posting to a mailbox; the owning shard's
// reactor watches read_fd() and calls drain() when it fires.
class Waker {
   public:
    Waker();
    ~Waker();
    Waker(const Waker&) = delete;
    Waker& operator=(const Waker&) = delete;

    int read_fd() const noexcept { return read_fd_; }
    void notify();  // wake the owning reactor (idempotent, cheap)
    void drain();   // consume pending notifications

   private:
    int read_fd_ = -1;
    int write_fd_ = -1;
};

}  // namespace blazekv

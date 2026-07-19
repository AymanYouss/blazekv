#include "blazekv/reactor.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "blazekv/build_config.hpp"

#if BLAZEKV_HAVE_EPOLL
#include <sys/epoll.h>
#endif
#if BLAZEKV_HAVE_URING
#include <liburing.h>
#endif
#if defined(__linux__)
#include <sys/eventfd.h>
#endif

namespace blazekv {

// ---------------------------------------------------------------------------
// poll(2) reactor: portable baseline used on macOS and anywhere epoll/uring are
// unavailable. O(n) per wait, but perfectly correct and adequate for tests.
// ---------------------------------------------------------------------------
class PollReactor final : public Reactor {
   public:
    bool add(int fd, std::uint32_t flags, void* token) override {
        index_[fd] = fds_.size();
        fds_.push_back(pollfd{fd, to_events(flags), 0});
        tokens_.push_back(token);
        return true;
    }
    bool mod(int fd, std::uint32_t flags, void* token) override {
        auto it = index_.find(fd);
        if (it == index_.end()) return add(fd, flags, token);
        fds_[it->second].events = to_events(flags);
        tokens_[it->second] = token;
        return true;
    }
    bool del(int fd) override {
        auto it = index_.find(fd);
        if (it == index_.end()) return false;
        std::size_t i = it->second;
        std::size_t last = fds_.size() - 1;
        if (i != last) {
            fds_[i] = fds_[last];
            tokens_[i] = tokens_[last];
            index_[fds_[i].fd] = i;
        }
        fds_.pop_back();
        tokens_.pop_back();
        index_.erase(it);
        return true;
    }
    int wait(ReadyEvent* out, int max, int timeout_ms) override {
        if (fds_.empty()) {
            if (timeout_ms > 0) ::poll(nullptr, 0, timeout_ms);
            return 0;
        }
        int n = ::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeout_ms);
        if (n <= 0) return n < 0 && errno == EINTR ? 0 : n;
        int produced = 0;
        for (std::size_t i = 0; i < fds_.size() && produced < max; ++i) {
            short re = fds_[i].revents;
            if (re == 0) continue;
            std::uint32_t flags = 0;
            if (re & (POLLIN | POLLHUP)) flags |= kRead;
            if (re & POLLOUT) flags |= kWrite;
            if (re & (POLLERR | POLLNVAL)) flags |= kError;
            out[produced++] = ReadyEvent{fds_[i].fd, tokens_[i], flags};
        }
        return produced;
    }
    const char* name() const override { return "poll"; }

   private:
    static short to_events(std::uint32_t flags) {
        short e = 0;
        if (flags & kRead) e |= POLLIN;
        if (flags & kWrite) e |= POLLOUT;
        return e;
    }
    std::vector<pollfd> fds_;
    std::vector<void*> tokens_;
    std::unordered_map<int, std::size_t> index_;
};

#if BLAZEKV_HAVE_EPOLL
// ---------------------------------------------------------------------------
// epoll(7) reactor: edge-agnostic level-triggered, O(1) registration.
// ---------------------------------------------------------------------------
class EpollReactor final : public Reactor {
   public:
    EpollReactor() : epfd_(::epoll_create1(EPOLL_CLOEXEC)) {}
    ~EpollReactor() override {
        if (epfd_ >= 0) ::close(epfd_);
    }
    bool add(int fd, std::uint32_t flags, void* token) override { return ctl(EPOLL_CTL_ADD, fd, flags, token); }
    bool mod(int fd, std::uint32_t flags, void* token) override { return ctl(EPOLL_CTL_MOD, fd, flags, token); }
    bool del(int fd) override { return ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr) == 0; }
    int wait(ReadyEvent* out, int max, int timeout_ms) override {
        if (max > kMaxBatch) max = kMaxBatch;
        int n = ::epoll_wait(epfd_, events_, max, timeout_ms);
        if (n < 0) return errno == EINTR ? 0 : -1;
        for (int i = 0; i < n; ++i) {
            std::uint32_t f = 0;
            if (events_[i].events & (EPOLLIN | EPOLLHUP)) f |= kRead;
            if (events_[i].events & EPOLLOUT) f |= kWrite;
            if (events_[i].events & EPOLLERR) f |= kError;
            out[i] = ReadyEvent{-1, events_[i].data.ptr, f};
        }
        return n;
    }
    const char* name() const override { return "epoll"; }

   private:
    static constexpr int kMaxBatch = 1024;
    bool ctl(int op, int fd, std::uint32_t flags, void* token) {
        epoll_event ev{};
        ev.data.ptr = token;
        if (flags & kRead) ev.events |= EPOLLIN;
        if (flags & kWrite) ev.events |= EPOLLOUT;
        return ::epoll_ctl(epfd_, op, fd, &ev) == 0;
    }
    int epfd_;
    epoll_event events_[kMaxBatch];
};
#endif  // BLAZEKV_HAVE_EPOLL

#if BLAZEKV_HAVE_URING
// ---------------------------------------------------------------------------
// io_uring reactor: uses multishot POLL_ADD so a single submission keeps
// delivering readiness for a fd without re-arming, giving epoll-like semantics
// on top of the uring completion queue. This is the fast path on modern Linux.
// ---------------------------------------------------------------------------
class UringReactor final : public Reactor {
   public:
    UringReactor() {
        io_uring_params p{};
        if (io_uring_queue_init_params(kQueueDepth, &ring_, &p) == 0) ready_ = true;
    }
    ~UringReactor() override {
        if (ready_) io_uring_queue_exit(&ring_);
    }
    bool ok() const { return ready_; }

    bool add(int fd, std::uint32_t flags, void* token) override {
        arm(fd, flags, token);
        io_uring_submit(&ring_);
        return true;
    }
    bool mod(int fd, std::uint32_t flags, void* token) override {
        // Cancel the outstanding multishot poll and re-arm with new interest.
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_poll_remove(sqe, reinterpret_cast<std::uint64_t>(token));
        arm(fd, flags, token);
        io_uring_submit(&ring_);
        return true;
    }
    bool del(int fd) override {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_cancel_fd(sqe, fd, 0);
        io_uring_submit(&ring_);
        return true;
    }
    int wait(ReadyEvent* out, int max, int timeout_ms) override {
        __kernel_timespec ts{};
        __kernel_timespec* tp = nullptr;
        if (timeout_ms >= 0) {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
            tp = &ts;
        }
        io_uring_cqe* cqe = nullptr;
        int r = io_uring_wait_cqe_timeout(&ring_, &cqe, tp);
        if (r < 0) return 0;  // -ETIME on timeout: no events
        int produced = 0;
        unsigned head;
        io_uring_for_each_cqe(&ring_, head, cqe) {
            if (produced >= max) break;
            void* token = io_uring_cqe_get_data(cqe);
            std::uint32_t f = 0;
            int res = cqe->res;
            if (res > 0) {
                if (res & (POLLIN | POLLHUP)) f |= kRead;
                if (res & POLLOUT) f |= kWrite;
                if (res & (POLLERR | POLLNVAL)) f |= kError;
            }
            if (token) out[produced++] = ReadyEvent{-1, token, f};
            io_uring_cqe_seen(&ring_, cqe);
        }
        return produced;
    }
    const char* name() const override { return "io_uring"; }

   private:
    static constexpr unsigned kQueueDepth = 4096;
    void arm(int fd, std::uint32_t flags, void* token) {
        unsigned mask = 0;
        if (flags & kRead) mask |= POLLIN;
        if (flags & kWrite) mask |= POLLOUT;
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_poll_multishot(sqe, fd, mask);
        io_uring_sqe_set_data(sqe, token);
    }
    io_uring ring_{};
    bool ready_ = false;
};
#endif  // BLAZEKV_HAVE_URING

std::unique_ptr<Reactor> make_reactor(ReactorKind kind) {
#if BLAZEKV_HAVE_URING
    if (kind == ReactorKind::Auto || kind == ReactorKind::Uring) {
        auto r = std::make_unique<UringReactor>();
        if (r->ok()) return r;
    }
#endif
#if BLAZEKV_HAVE_EPOLL
    if (kind == ReactorKind::Auto || kind == ReactorKind::Epoll || kind == ReactorKind::Uring) {
        return std::make_unique<EpollReactor>();
    }
#endif
    return std::make_unique<PollReactor>();
}

// ---------------------------------------------------------------------------
// Waker
// ---------------------------------------------------------------------------
Waker::Waker() {
#if defined(__linux__)
    read_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    write_fd_ = read_fd_;  // eventfd is read+write on one fd
#else
    int fds[2];
    if (::pipe(fds) == 0) {
        ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
        ::fcntl(fds[1], F_SETFL, O_NONBLOCK);
        read_fd_ = fds[0];
        write_fd_ = fds[1];
    }
#endif
}

Waker::~Waker() {
    if (read_fd_ >= 0) ::close(read_fd_);
    if (write_fd_ >= 0 && write_fd_ != read_fd_) ::close(write_fd_);
}

void Waker::notify() {
    std::uint64_t one = 1;
    ssize_t r = ::write(write_fd_, &one, sizeof(one));
    (void)r;  // EAGAIN just means a wakeup is already pending
}

void Waker::drain() {
    std::uint64_t buf[16];
    while (::read(read_fd_, buf, sizeof(buf)) > 0) {
    }
}

}  // namespace blazekv

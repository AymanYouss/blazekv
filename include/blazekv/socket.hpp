#pragma once

#include <cstdint>
#include <string>

namespace blazekv {

// Thin RAII wrapper around a file descriptor.
class FileDesc {
   public:
    FileDesc() = default;
    explicit FileDesc(int fd) : fd_(fd) {}
    FileDesc(FileDesc&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    FileDesc& operator=(FileDesc&& o) noexcept {
        if (this != &o) {
            close();
            fd_ = o.fd_;
            o.fd_ = -1;
        }
        return *this;
    }
    FileDesc(const FileDesc&) = delete;
    FileDesc& operator=(const FileDesc&) = delete;
    ~FileDesc() { close(); }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept {
        int f = fd_;
        fd_ = -1;
        return f;
    }
    void close() noexcept;

   private:
    int fd_ = -1;
};

// Non-blocking socket helpers. All return -1 and set errno on failure.
namespace net {

// Creates a listening TCP socket bound to host:port. When reuseport is set the
// kernel load-balances accepted connections across every shard's listener, which
// is how BlazeKV distributes clients over cores with no user-space accept lock.
int listen_tcp(const std::string& host, std::uint16_t port, int backlog, bool reuseport,
               std::string& err);

int set_nonblocking(int fd);
int set_nodelay(int fd, bool on);
int set_reuseaddr(int fd);

// accept4-style accept returning a non-blocking fd, or -1 (EAGAIN when drained).
int accept_conn(int listen_fd);

// Pins the calling thread to `core` (best effort; no-op on platforms without
// affinity control such as macOS).
bool pin_to_core(unsigned core);

}  // namespace net
}  // namespace blazekv

#include "blazekv/persist.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace blazekv {

bool Aof::open(const std::string& path, FsyncPolicy policy, std::string& err) {
    path_ = path;
    policy_ = policy;
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        err = std::string("open aof: ") + std::strerror(errno);
        return false;
    }
    last_fsync_ms_ = now_ms();
    return true;
}

void Aof::append(std::string_view encoded) {
    if (fd_ < 0) return;
    buf_.append(encoded);
    // Coalesce writes; flush when the batch is sizable or on Always for durability.
    if (buf_.size() >= 16 * 1024 || policy_ == FsyncPolicy::Always) flush();
    dirty_since_fsync_ = true;
    if (policy_ == FsyncPolicy::Always) do_fsync();
}

void Aof::flush() {
    if (fd_ < 0 || buf_.empty()) return;
    const char* p = buf_.data();
    std::size_t remaining = buf_.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd_, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;  // best effort; the shard keeps serving
        }
        p += n;
        remaining -= static_cast<std::size_t>(n);
        bytes_written_ += static_cast<std::uint64_t>(n);
    }
    buf_.clear();
}

void Aof::tick() {
    if (fd_ < 0) return;
    if (policy_ == FsyncPolicy::EverySec) {
        const std::uint64_t now = now_ms();
        if (dirty_since_fsync_ && now - last_fsync_ms_ >= 1000) {
            flush();
            do_fsync();
            last_fsync_ms_ = now;
        }
    }
}

void Aof::do_fsync() {
    if (fd_ < 0) return;
#if defined(__APPLE__)
    ::fcntl(fd_, F_FULLFSYNC);
#else
    ::fdatasync(fd_);
#endif
    dirty_since_fsync_ = false;
}

void Aof::reset_file(const std::string& path, std::string& err) {
    close();
    ::unlink(path.c_str());
    open(path, policy_, err);
}

void Aof::close() {
    if (fd_ >= 0) {
        flush();
        do_fsync();
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace blazekv

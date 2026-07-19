#include "blazekv/socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <sched.h>
#endif

namespace blazekv {

void FileDesc::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

namespace net {

int set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_nodelay(int fd, bool on) {
    int v = on ? 1 : 0;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

int set_reuseaddr(int fd) {
    int v = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
}

int listen_tcp(const std::string& host, std::uint16_t port, int backlog, bool reuseport,
               std::string& err) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    set_reuseaddr(fd);
#ifdef SO_REUSEPORT
    if (reuseport) {
        int v = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
    }
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0" || host == "*") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        err = "invalid bind address: " + host;
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        err = std::string("bind: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, backlog) < 0) {
        err = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    set_nonblocking(fd);
    return fd;
}

int accept_conn(int listen_fd) {
#if defined(__linux__) && defined(SOCK_NONBLOCK)
    int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK);
    if (fd < 0) return -1;
    return fd;
#else
    int fd = ::accept(listen_fd, nullptr, nullptr);
    if (fd < 0) return -1;
    set_nonblocking(fd);
    return fd;
#endif
}

bool pin_to_core(unsigned core) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core % static_cast<unsigned>(CPU_SETSIZE), &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)core;
    return false;  // affinity control is unavailable on this platform
#endif
}

}  // namespace net
}  // namespace blazekv

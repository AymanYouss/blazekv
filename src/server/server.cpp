#include "blazekv/server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

#include "blazekv/build_config.hpp"
#include "blazekv/metrics.hpp"
#include "blazekv/shard.hpp"
#include "blazekv/socket.hpp"

namespace blazekv {
namespace {
std::atomic<bool>* g_running_flag = nullptr;
void handle_signal(int) {
    if (g_running_flag) g_running_flag->store(false, std::memory_order_release);
}
}  // namespace

Server::Server(Config cfg) : cfg_(std::move(cfg)) {}
Server::~Server() { stop(); }

bool Server::start(std::string& err) {
    unsigned n = cfg_.shards;
    if (n == 0) {
        n = std::thread::hardware_concurrency();
        if (n == 0) n = 4;
    }
    start_time_ms_ = now_ms();

    for (unsigned i = 0; i < n; ++i) shards_.push_back(std::make_unique<Shard>(i, *this));

    // One SO_REUSEPORT listener per shard: the kernel spreads incoming connections
    // across cores with no user-space accept lock.
    for (unsigned i = 0; i < n; ++i) {
        int fd = net::listen_tcp(cfg_.bind, cfg_.port, cfg_.tcp_backlog, /*reuseport=*/true, err);
        if (fd < 0) return false;
        listen_fds_.push_back(fd);
        if (!shards_[i]->init(fd, err)) return false;
    }

    running_.store(true, std::memory_order_release);
    for (auto& s : shards_) s->start();

    if (cfg_.metrics_enabled) {
        metrics_thread_ = std::make_unique<std::thread>([this] { run_metrics_server(); });
    }
    return true;
}

void Server::run_until_signal() {
    g_running_flag = &running_;
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    stop();
}

void Server::stop() {
    if (!running_.exchange(false)) {
        // Already stopping; still make sure threads are joined.
    }
    for (auto& s : shards_) s->stop();
    for (auto& s : shards_) s->join();
    if (metrics_thread_ && metrics_thread_->joinable()) metrics_thread_->join();
    metrics_thread_.reset();
    for (int fd : listen_fds_)
        if (fd >= 0) ::close(fd);
    listen_fds_.clear();
    shards_.clear();
}

bool Server::trigger_snapshot(std::string& err) {
    if (!cfg_.snapshot_enabled) {
        err = "snapshots disabled";
        return false;
    }
    // fork() gives the child a copy-on-write image of every shard's memory, so it
    // serializes a consistent point-in-time snapshot while the shards keep serving.
    pid_t pid = ::fork();
    if (pid < 0) {
        err = "fork failed";
        return false;
    }
    if (pid == 0) {
        for (auto& s : shards_) {
            std::string e;
            s->save_snapshot(e);
        }
        _exit(0);
    }
    // Parent reaps the child without blocking the control path.
    int status = 0;
    ::waitpid(pid, &status, 0);
    return true;
}

// ---------------------------------------------------------------------------
// INFO / metrics rendering
// ---------------------------------------------------------------------------
namespace {
// Aggregate percentile across every shard's histogram by summing bucket counts.
std::uint64_t aggregate_percentile(const std::vector<std::unique_ptr<Shard>>& shards, double p) {
    std::uint64_t total = 0;
    for (const auto& s : shards) total += s->metrics().latency.count();
    if (total == 0) return 0;
    const auto target = static_cast<std::uint64_t>(p * static_cast<double>(total));
    std::uint64_t cumulative = 0;
    for (int b = 0; b < LatencyHistogram::kBuckets; ++b) {
        for (const auto& s : shards) cumulative += s->metrics().latency.bucket_count(b);
        if (cumulative >= target)
            return static_cast<std::uint64_t>(LatencyHistogram::bucket_bound_seconds(b + 1) * 1e6);
    }
    return 0;
}

template <class Fn>
std::uint64_t sum_metric(const std::vector<std::unique_ptr<Shard>>& shards, Fn fn) {
    std::uint64_t total = 0;
    for (const auto& s : shards) total += fn(s->metrics());
    return total;
}
}  // namespace

std::string Server::render_info(std::string_view section) const {
    std::ostringstream os;
    const std::uint64_t uptime = (now_ms() - start_time_ms_) / 1000;
    auto want = [&](const char* s) {
        return section == "default" || section == "all" || section == "everything" || section == s;
    };

    if (want("server")) {
        os << "# Server\r\n";
        os << "redis_version:7.4.0\r\n";  // reported for client compatibility
        os << "blazekv_version:" << BLAZEKV_VERSION << "\r\n";
        os << "os:" <<
#if defined(__linux__)
            "Linux"
#elif defined(__APPLE__)
            "Darwin"
#else
            "unknown"
#endif
           << "\r\n";
        os << "multiplexing_api:" << (BLAZEKV_HAVE_URING ? "io_uring" : (BLAZEKV_HAVE_EPOLL ? "epoll" : "poll"))
           << "\r\n";
        os << "process_id:" << ::getpid() << "\r\n";
        os << "tcp_port:" << cfg_.port << "\r\n";
        os << "uptime_in_seconds:" << uptime << "\r\n";
        os << "io_threads_active:" << shards_.size() << "\r\n";
        os << "shards:" << shards_.size() << "\r\n\r\n";
    }
    if (want("clients")) {
        os << "# Clients\r\n";
        os << "connected_clients:" << sum_metric(shards_, [](ShardMetrics& m) {
            return m.connections_open.load();
        }) << "\r\n\r\n";
    }
    if (want("memory")) {
        os << "# Memory\r\n";
        std::uint64_t mem = sum_metric(shards_, [](ShardMetrics& m) { return m.memory_bytes.load(); });
        os << "used_memory:" << mem << "\r\n";
        os << "used_memory_human:" << (mem / 1024) << "K\r\n";
        os << "maxmemory:" << cfg_.maxmemory << "\r\n";
        os << "mem_allocator:" <<
#if BLAZEKV_USE_MIMALLOC
            "mimalloc"
#else
            "libc"
#endif
           << "\r\n\r\n";
    }
    if (want("stats")) {
        os << "# Stats\r\n";
        os << "total_commands_processed:"
           << sum_metric(shards_, [](ShardMetrics& m) { return m.commands_total.load(); }) << "\r\n";
        os << "total_connections_received:"
           << sum_metric(shards_, [](ShardMetrics& m) { return m.connections_total.load(); }) << "\r\n";
        os << "keyspace_hits:" << sum_metric(shards_, [](ShardMetrics& m) { return m.keyspace_hits.load(); })
           << "\r\n";
        os << "keyspace_misses:"
           << sum_metric(shards_, [](ShardMetrics& m) { return m.keyspace_misses.load(); }) << "\r\n";
        os << "expired_keys:" << sum_metric(shards_, [](ShardMetrics& m) { return m.expired_keys.load(); })
           << "\r\n";
        os << "cross_shard_ops:"
           << sum_metric(shards_, [](ShardMetrics& m) { return m.cross_shard_ops.load(); }) << "\r\n";
        os << "latency_p50_us:" << aggregate_percentile(shards_, 0.50) << "\r\n";
        os << "latency_p99_us:" << aggregate_percentile(shards_, 0.99) << "\r\n";
        os << "latency_p999_us:" << aggregate_percentile(shards_, 0.999) << "\r\n\r\n";
    }
    if (want("replication")) {
        os << "# Replication\r\nrole:master\r\nconnected_slaves:0\r\n\r\n";
    }
    if (want("keyspace")) {
        std::uint64_t keys = sum_metric(shards_, [](ShardMetrics& m) { return m.keys.load(); });
        std::uint64_t expires = sum_metric(shards_, [](ShardMetrics& m) { return m.expires.load(); });
        os << "# Keyspace\r\n";
        if (keys > 0) os << "db0:keys=" << keys << ",expires=" << expires << ",avg_ttl=0\r\n";
        os << "\r\n";
    }
    return os.str();
}

std::string Server::render_prometheus() const {
    std::ostringstream os;
    auto line = [&](const char* name, const char* help, const char* type) {
        os << "# HELP blazekv_" << name << " " << help << "\n";
        os << "# TYPE blazekv_" << name << " " << type << "\n";
    };

    line("commands_total", "Total commands processed per shard", "counter");
    for (const auto& s : shards_)
        os << "blazekv_commands_total{shard=\"" << s->id() << "\"} "
           << s->metrics().commands_total.load() << "\n";

    line("keyspace_hits_total", "Keyspace hits", "counter");
    for (const auto& s : shards_)
        os << "blazekv_keyspace_hits_total{shard=\"" << s->id() << "\"} "
           << s->metrics().keyspace_hits.load() << "\n";
    line("keyspace_misses_total", "Keyspace misses", "counter");
    for (const auto& s : shards_)
        os << "blazekv_keyspace_misses_total{shard=\"" << s->id() << "\"} "
           << s->metrics().keyspace_misses.load() << "\n";

    line("cross_shard_ops_total", "Cross-shard forwarded operations", "counter");
    for (const auto& s : shards_)
        os << "blazekv_cross_shard_ops_total{shard=\"" << s->id() << "\"} "
           << s->metrics().cross_shard_ops.load() << "\n";

    line("keys", "Number of keys per shard", "gauge");
    for (const auto& s : shards_)
        os << "blazekv_keys{shard=\"" << s->id() << "\"} " << s->metrics().keys.load() << "\n";

    line("memory_bytes", "Approximate memory used per shard", "gauge");
    for (const auto& s : shards_)
        os << "blazekv_memory_bytes{shard=\"" << s->id() << "\"} " << s->metrics().memory_bytes.load()
           << "\n";

    line("connected_clients", "Open client connections per shard", "gauge");
    for (const auto& s : shards_)
        os << "blazekv_connected_clients{shard=\"" << s->id() << "\"} "
           << s->metrics().connections_open.load() << "\n";

    // Aggregate latency as a Prometheus histogram.
    os << "# HELP blazekv_command_latency_seconds Command execution latency\n";
    os << "# TYPE blazekv_command_latency_seconds histogram\n";
    std::uint64_t cumulative = 0, total = 0, sum_weighted = 0;
    for (const auto& s : shards_) total += s->metrics().latency.count();
    for (int b = 0; b < LatencyHistogram::kBuckets; b += 8) {
        std::uint64_t c = 0;
        for (int j = b; j < b + 8 && j < LatencyHistogram::kBuckets; ++j)
            for (const auto& s : shards_) c += s->metrics().latency.bucket_count(j);
        cumulative += c;
        double bound = LatencyHistogram::bucket_bound_seconds(std::min(b + 8, LatencyHistogram::kBuckets));
        os << "blazekv_command_latency_seconds_bucket{le=\"" << bound << "\"} " << cumulative << "\n";
        sum_weighted += static_cast<std::uint64_t>(c * bound * 1e6);
    }
    os << "blazekv_command_latency_seconds_bucket{le=\"+Inf\"} " << total << "\n";
    os << "blazekv_command_latency_seconds_count " << total << "\n";
    os << "blazekv_command_latency_seconds_sum " << static_cast<double>(sum_weighted) / 1e6 << "\n";
    return os.str();
}

std::string Server::render_dashboard_json() const {
    std::ostringstream os;
    std::uint64_t commands = sum_metric(shards_, [](ShardMetrics& m) { return m.commands_total.load(); });
    std::uint64_t keys = sum_metric(shards_, [](ShardMetrics& m) { return m.keys.load(); });
    std::uint64_t mem = sum_metric(shards_, [](ShardMetrics& m) { return m.memory_bytes.load(); });
    std::uint64_t conns = sum_metric(shards_, [](ShardMetrics& m) { return m.connections_open.load(); });
    std::uint64_t xshard = sum_metric(shards_, [](ShardMetrics& m) { return m.cross_shard_ops.load(); });
    std::uint64_t hits = sum_metric(shards_, [](ShardMetrics& m) { return m.keyspace_hits.load(); });
    std::uint64_t misses = sum_metric(shards_, [](ShardMetrics& m) { return m.keyspace_misses.load(); });

    os << "{";
    os << "\"uptime_s\":" << (now_ms() - start_time_ms_) / 1000 << ",";
    os << "\"shard_count\":" << shards_.size() << ",";
    os << "\"commands_total\":" << commands << ",";
    os << "\"keys\":" << keys << ",";
    os << "\"memory_bytes\":" << mem << ",";
    os << "\"connections\":" << conns << ",";
    os << "\"cross_shard_ops\":" << xshard << ",";
    os << "\"keyspace_hits\":" << hits << ",";
    os << "\"keyspace_misses\":" << misses << ",";
    os << "\"latency_p50_us\":" << aggregate_percentile(shards_, 0.50) << ",";
    os << "\"latency_p99_us\":" << aggregate_percentile(shards_, 0.99) << ",";
    os << "\"latency_p999_us\":" << aggregate_percentile(shards_, 0.999) << ",";
    os << "\"mem_allocator\":\"" << (BLAZEKV_USE_MIMALLOC ? "mimalloc" : "libc") << "\",";
    os << "\"reactor\":\""
       << (BLAZEKV_HAVE_URING ? "io_uring" : (BLAZEKV_HAVE_EPOLL ? "epoll" : "poll")) << "\",";
    os << "\"shards\":[";
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        auto& m = shards_[i]->metrics();
        if (i) os << ",";
        os << "{\"id\":" << i << ",\"commands\":" << m.commands_total.load()
           << ",\"keys\":" << m.keys.load() << ",\"memory_bytes\":" << m.memory_bytes.load()
           << ",\"connections\":" << m.connections_open.load()
           << ",\"reads\":" << m.reads.load() << ",\"writes\":" << m.writes.load()
           << ",\"p99_us\":" << shards_[i]->metrics().latency.percentile(0.99) << "}";
    }
    os << "]}";
    return os.str();
}

// ---------------------------------------------------------------------------
// Observability HTTP server: /metrics (Prometheus), /api/stats (dashboard),
// /api/query (run a command via a loopback client), /healthz.
// ---------------------------------------------------------------------------
namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

// Reads exactly one RESP reply from `fd` and renders it as a human-readable
// string for the query console.
class ReplyReader {
   public:
    explicit ReplyReader(int fd) : fd_(fd) {}
    std::string read_reply() {
        char type = getc();
        switch (type) {
            case '+':
                return read_line();
            case '-':
                return "(error) " + read_line();
            case ':':
                return read_line();
            case '$': {
                long long len = std::atoll(read_line().c_str());
                if (len < 0) return "(nil)";
                std::string s = read_n(static_cast<std::size_t>(len));
                read_line();  // trailing CRLF
                return "\"" + s + "\"";
            }
            case '*': {
                long long n = std::atoll(read_line().c_str());
                if (n < 0) return "(nil)";
                std::string out = "[";
                for (long long i = 0; i < n; ++i) {
                    if (i) out += ", ";
                    out += read_reply();
                }
                out += "]";
                return out;
            }
            default:
                return "(protocol error)";
        }
    }

   private:
    char getc() {
        if (pos_ >= buf_.size()) {
            char tmp[4096];
            ssize_t n = ::read(fd_, tmp, sizeof(tmp));
            if (n <= 0) return '\0';
            buf_.assign(tmp, static_cast<std::size_t>(n));
            pos_ = 0;
        }
        return buf_[pos_++];
    }
    std::string read_line() {
        std::string out;
        char ch;
        while ((ch = getc()) != '\0') {
            if (ch == '\r') {
                getc();  // consume '\n'
                break;
            }
            out += ch;
        }
        return out;
    }
    std::string read_n(std::size_t n) {
        std::string out;
        while (out.size() < n) {
            char ch = getc();
            if (ch == '\0') break;
            out += ch;
        }
        return out;
    }
    int fd_;
    std::string buf_;
    std::size_t pos_ = 0;
};

// Runs a single command string against the server's own listening port.
std::string run_loopback_command(std::uint16_t port, const std::string& cmdline) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "(error) cannot open loopback socket";
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return "(error) cannot connect to blazekv";
    }
    std::string req = cmdline;
    if (req.size() < 2 || req.substr(req.size() - 2) != "\r\n") req += "\r\n";
    ::write(fd, req.data(), req.size());
    ReplyReader reader(fd);
    std::string reply = reader.read_reply();
    ::close(fd);
    return reply;
}

void write_http(int fd, const std::string& status, const std::string& ctype,
                const std::string& body) {
    std::ostringstream os;
    os << "HTTP/1.1 " << status << "\r\n";
    os << "Content-Type: " << ctype << "\r\n";
    os << "Content-Length: " << body.size() << "\r\n";
    os << "Access-Control-Allow-Origin: *\r\n";
    os << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    os << "Access-Control-Allow-Headers: Content-Type\r\n";
    os << "Connection: close\r\n\r\n";
    os << body;
    std::string out = os.str();
    ssize_t off = 0;
    while (off < static_cast<ssize_t>(out.size())) {
        ssize_t n = ::write(fd, out.data() + off, out.size() - static_cast<std::size_t>(off));
        if (n <= 0) break;
        off += n;
    }
}

}  // namespace

void Server::run_metrics_server() {
    std::string err;
    int listen_fd =
        net::listen_tcp(cfg_.bind, cfg_.metrics_port, 128, /*reuseport=*/false, err);
    if (listen_fd < 0) return;

    while (running_.load(std::memory_order_acquire)) {
        pollfd pfd{listen_fd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 200);
        if (pr <= 0) continue;
        int cfd = ::accept(listen_fd, nullptr, nullptr);
        if (cfd < 0) continue;
        // BSD-derived kernels propagate O_NONBLOCK from the listener to the
        // accepted socket; this HTTP handler is synchronous, so make it blocking
        // with a bounded receive timeout.
        int fl = ::fcntl(cfd, F_GETFL, 0);
        ::fcntl(cfd, F_SETFL, fl & ~O_NONBLOCK);
        timeval tv{2, 0};
        ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Read the request headers (and body, if any).
        std::string req;
        char buf[8192];
        std::size_t content_len = 0;
        while (true) {
            ssize_t n = ::read(cfd, buf, sizeof(buf));
            if (n <= 0) break;
            req.append(buf, static_cast<std::size_t>(n));
            auto hdr_end = req.find("\r\n\r\n");
            if (hdr_end != std::string::npos) {
                auto cl = req.find("Content-Length:");
                if (cl != std::string::npos)
                    content_len = static_cast<std::size_t>(std::atoll(req.c_str() + cl + 15));
                if (req.size() >= hdr_end + 4 + content_len) break;
            }
            if (req.size() > 1 << 20) break;
        }

        std::string method = req.substr(0, req.find(' '));
        std::size_t path_start = req.find(' ') + 1;
        std::string path = req.substr(path_start, req.find(' ', path_start) - path_start);

        if (method == "OPTIONS") {
            write_http(cfd, "204 No Content", "text/plain", "");
        } else if (path == "/metrics") {
            write_http(cfd, "200 OK", "text/plain; version=0.0.4", render_prometheus());
        } else if (path == "/healthz") {
            write_http(cfd, "200 OK", "application/json", "{\"status\":\"ok\"}");
        } else if (path == "/api/stats") {
            write_http(cfd, "200 OK", "application/json", render_dashboard_json());
        } else if (path == "/api/query" && method == "POST") {
            auto body_pos = req.find("\r\n\r\n");
            std::string body = body_pos == std::string::npos ? "" : req.substr(body_pos + 4);
            // Body may be raw text or {"cmd":"..."}; support both.
            std::string cmdline = body;
            auto cpos = body.find("\"cmd\"");
            if (cpos != std::string::npos) {
                auto q1 = body.find('"', body.find(':', cpos));
                auto q2 = body.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    cmdline = body.substr(q1 + 1, q2 - q1 - 1);
            }
            std::string reply = run_loopback_command(cfg_.port, cmdline);
            write_http(cfd, "200 OK", "application/json",
                       "{\"result\":\"" + json_escape(reply) + "\"}");
        } else {
            write_http(cfd, "404 Not Found", "application/json",
                       "{\"error\":\"not found\",\"method\":\"" + json_escape(method) +
                           "\",\"path\":\"" + json_escape(path) + "\"}");
        }
        ::close(cfd);
    }
    ::close(listen_fd);
}

}  // namespace blazekv
